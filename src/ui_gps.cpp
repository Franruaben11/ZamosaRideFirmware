#include "ui_gps.h"
#include <cmath>
#include "display_hal.h"
#include "icons/nav_icons.h"

static UiState current_ui_state = UI_STATE_DISCONNECTED;
static int last_rendered_minute = -2;
static uint32_t disconnected_state_entered_ms = 0;

static TelemetryData current_telemetry;
static lv_point_precise_t current_route_points[MAX_ROUTE_POINTS];

static lv_obj_t *scr_gps = nullptr;

// Widgets: Modo Standby Desconectado
static lv_obj_t *lbl_disc_icon     = nullptr;
static lv_obj_t *lbl_disc_title    = nullptr;
static lv_obj_t *lbl_disc_subtitle = nullptr;
static lv_obj_t *lbl_disc_status   = nullptr;

// Widgets: Modo Standby Conectado
static lv_obj_t *lbl_conn_status = nullptr;
static lv_obj_t *lbl_conn_clock  = nullptr;
static lv_obj_t *lbl_conn_ready  = nullptr;

// Widgets: Modo Navegación Activa (Estética minimalista tipo Beeline Moto II)
static lv_style_t style_route;
static lv_obj_t *route_line         = nullptr;
static lv_obj_t *img_pilot          = nullptr;
static lv_obj_t *img_maniobra       = nullptr;
static lv_obj_t *lbl_distancia_num  = nullptr;
static lv_obj_t *lbl_distancia_unit = nullptr;
static lv_obj_t *arc_progreso       = nullptr;

// Widgets: Alerta de Radar en Navegación Activa (Sector medio-izquierdo a las 9 en punto)
static lv_obj_t *badge_radar        = nullptr;
static lv_obj_t *lbl_radar_val      = nullptr;
static lv_obj_t *lbl_radar_dist     = nullptr;

static void set_obj_visible(lv_obj_t *obj, bool visible) {
    if (obj == nullptr) return;
    lv_obj_set_hidden(obj, !visible);
}

// Proyecta puntosRuta para anclar puntosRuta[0] en (X: 233, Y: 255) y ampliar el minimapa en la mitad superior (Y: 12..255, ancho útil ~340 px)
static void project_route_points(const lv_point_precise_t *raw_pts, size_t count, lv_point_precise_t *out_pts) {
    if (count == 0) return;

    const float anchor_x   = 233.0f;
    const float anchor_y   = 255.0f;
    const float min_y      = 12.0f;
    const float max_y      = 255.0f;
    const float half_width = 170.0f; // Ancho útil total de 340 px (X: 63..403)
    const float min_x      = anchor_x - half_width;
    const float max_x      = anchor_x + half_width;
    const float usable_h   = anchor_y - min_y; // 243 px de recorrido vertical superior

    float x0 = (float)raw_pts[0].x;
    float y0 = (float)raw_pts[0].y;

    out_pts[0].x = (lv_value_precise_t)anchor_x;
    out_pts[0].y = (lv_value_precise_t)anchor_y;

    if (count == 1) return;

    float max_up = 0.0f;
    float max_side = 0.0f;
    for (size_t i = 1; i < count; ++i) {
        float dx = (float)raw_pts[i].x - x0;
        float dy = (float)raw_pts[i].y - y0;
        if (dy < 0.0f && (-dy) > max_up) {
            max_up = -dy;
        }
        float abs_dx = fabsf(dx);
        if (abs_dx > max_side) {
            max_side = abs_dx;
        }
    }

    // Escalar para aprovechar el área de 340x243 px con mayor presencia visual en pantalla
    float scale_y = (max_up > 1.0f) ? (usable_h / max_up) : 1.35f;
    float scale_x = (max_side > 1.0f) ? (half_width / max_side) : 1.35f;
    float scale = (scale_x < scale_y) ? scale_x : scale_y;
    if (scale > 1.45f) {
        scale = 1.45f;
    }

    for (size_t i = 1; i < count; ++i) {
        float dx = (float)raw_pts[i].x - x0;
        float dy = (float)raw_pts[i].y - y0;

        float px = anchor_x + dx * scale;
        float py = anchor_y + dy * scale;

        if (px < min_x) px = min_x;
        if (px > max_x) px = max_x;
        if (py < min_y) py = min_y;
        if (py > max_y) py = max_y;

        out_pts[i].x = (lv_value_precise_t)lroundf(px);
        out_pts[i].y = (lv_value_precise_t)lroundf(py);
    }
}

static const lv_image_dsc_t *get_maneuver_icon(int tipo_maniobra) {
    switch (tipo_maniobra) {
        case 1:
            return &icon_turn_left;   // 1: Doblar a la izquierda
        case 2:
            return &icon_turn_right;  // 2: Doblar a la derecha
        case 3:
            return &icon_straight;    // 3: Seguir derecho
        case 4:
            return &icon_finish;      // 4: Llegada a destino
        case 5:
            return &icon_uturn;       // 5: Retorno en U
        default:
            return &icon_straight;
    }
}

lv_obj_t *ui_gps_get_screen() {
    return scr_gps;
}

UiState ui_gps_get_state() {
    return current_ui_state;
}

uint32_t ui_gps_get_disconnected_entered_ms() {
    return disconnected_state_entered_ms;
}

void ui_gps_reset_disconnected_timer(uint32_t now_ms) {
    disconnected_state_entered_ms = now_ms;
}

void ui_gps_update_standby_clock() {
    if (lbl_conn_clock == nullptr) return;

    uint32_t base_seconds = 0;
    uint32_t base_millis = 0;
    bool synced = ble_get_synced_time(&base_seconds, &base_millis);

    if (!synced) {
        if (last_rendered_minute != -1) {
            last_rendered_minute = -1;
            lv_label_set_text(lbl_conn_clock, "--:--");
        }
        return;
    }

    uint32_t elapsed_sec = (uint32_t)(millis() - base_millis) / 1000U;
    uint32_t now_day_sec = (base_seconds + elapsed_sec) % SECONDS_PER_DAY;
    int minute_of_day = (int)(now_day_sec / 60U);

    // Solo redibujar cuando cambie el minuto para evitar volcados QSPI innecesarios
    if (minute_of_day != last_rendered_minute) {
        last_rendered_minute = minute_of_day;
        uint32_t hh = now_day_sec / 3600U;
        uint32_t mm = (now_day_sec % 3600U) / 60U;
        lv_label_set_text_fmt(lbl_conn_clock, "%02u:%02u", (unsigned int)hh, (unsigned int)mm);
    }
}

void ui_gps_apply_state(UiState new_state, ActiveScreen active_screen) {
    current_ui_state = new_state;

    bool show_disc = (new_state == UI_STATE_DISCONNECTED);
    bool show_conn = (new_state == UI_STATE_CONNECTED_STANDBY);
    bool show_nav  = (new_state == UI_STATE_NAVIGATING);

    // Escalado dinámico de frecuencia de CPU y gestión de brillo/encendido AMOLED
    if (show_nav || active_screen == SCREEN_TELEMETRY) {
        setCpuFrequencyMhz(show_nav ? 160 : 80);
        display_turn_on();
        display_set_brightness(255);
    } else if (show_conn) {
        setCpuFrequencyMhz(80);
        display_turn_on();
        display_set_brightness(160);
    } else {
        setCpuFrequencyMhz(80);
        disconnected_state_entered_ms = millis();
        display_turn_on();
        display_set_brightness(60);
    }

    set_obj_visible(lbl_disc_icon, show_disc);
    set_obj_visible(lbl_disc_title, show_disc);
    set_obj_visible(lbl_disc_subtitle, show_disc);
    set_obj_visible(lbl_disc_status, show_disc);

    set_obj_visible(lbl_conn_status, show_conn);
    set_obj_visible(lbl_conn_clock, show_conn);
    set_obj_visible(lbl_conn_ready, show_conn);

    set_obj_visible(route_line, show_nav);
    set_obj_visible(img_pilot, show_nav);
    set_obj_visible(img_maniobra, show_nav);
    set_obj_visible(lbl_distancia_num, show_nav);
    set_obj_visible(lbl_distancia_unit, show_nav);
    set_obj_visible(arc_progreso, show_nav);

    bool show_radar = show_nav && (current_telemetry.limiteVelocidad > 0 || current_telemetry.limiteVelocidad == -1);
    set_obj_visible(badge_radar, show_radar);
    set_obj_visible(lbl_radar_dist, show_radar);

    if (show_conn) {
        last_rendered_minute = -2; // Forzar actualización inmediata del reloj al entrar a Standby Conectado
        ui_gps_update_standby_clock();
    }
}

void ui_gps_init(bool initially_connected) {
    scr_gps = lv_screen_active();
    lv_obj_t *scr = scr_gps;

    // ------------------------------------------------------------------------
    // 1. Modo Standby: Desconectado
    // ------------------------------------------------------------------------
    lbl_disc_icon = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_disc_icon, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_disc_icon, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_label_set_text(lbl_disc_icon, LV_SYMBOL_BLUETOOTH);
    lv_obj_align(lbl_disc_icon, LV_ALIGN_CENTER, 0, -64);

    lbl_disc_title = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_disc_title, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_disc_title, lv_color_hex(0xEDEBEB), LV_PART_MAIN);
    lv_label_set_text(lbl_disc_title, "ZAMOSARIDE");
    lv_obj_align(lbl_disc_title, LV_ALIGN_CENTER, 0, -18);

    lbl_disc_subtitle = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_disc_subtitle, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_disc_subtitle, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_label_set_text(lbl_disc_subtitle, "CONECTA TU DISPOSITIVO");
    lv_obj_align(lbl_disc_subtitle, LV_ALIGN_CENTER, 0, 22);

    lbl_disc_status = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_disc_status, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_disc_status, lv_color_hex(0x555555), LV_PART_MAIN);
    lv_label_set_text(lbl_disc_status, "Esperando conexion...");
    lv_obj_align(lbl_disc_status, LV_ALIGN_CENTER, 0, 56);

    // ------------------------------------------------------------------------
    // 2. Modo Standby: Conectado (Sin navegación activa)
    // ------------------------------------------------------------------------
    lbl_conn_status = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_conn_status, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_conn_status, lv_color_hex(0x50C8A0), LV_PART_MAIN);
    lv_label_set_text(lbl_conn_status, LV_SYMBOL_BLUETOOTH " CONECTADO");
    lv_obj_align(lbl_conn_status, LV_ALIGN_CENTER, 0, -72);

    lbl_conn_clock = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_conn_clock, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_conn_clock, lv_color_hex(0xEDEBEB), LV_PART_MAIN);
    lv_label_set_text(lbl_conn_clock, "--:--");
    lv_obj_align(lbl_conn_clock, LV_ALIGN_CENTER, 0, -6);

    lbl_conn_ready = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_conn_ready, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_conn_ready, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_label_set_text(lbl_conn_ready, "LISTO PARA NAVEGAR");
    lv_obj_align(lbl_conn_ready, LV_ALIGN_CENTER, 0, 56);

    // ------------------------------------------------------------------------
    // 3. Modo Navegación Activa (Estética Beeline Moto II - 3 Zonas sobre #000000)
    // ------------------------------------------------------------------------
    // A. Zona Superior / Central: Mapa Vectorial (Y: 0..260) + Puntero Chevron
    lv_style_init(&style_route);
    lv_style_set_line_color(&style_route, lv_color_hex(0xEDEBEB));
    lv_style_set_line_width(&style_route, 18);
    lv_style_set_line_rounded(&style_route, true);

    route_line = lv_line_create(scr);
    lv_obj_set_pos(route_line, 0, 0);
    lv_obj_add_style(route_line, &style_route, LV_PART_MAIN);
    current_telemetry.numPuntosRuta = 0;
    lv_line_set_points(route_line, current_route_points, 0);

    // Puntero del vehículo centrado en (X: 233, Y: 260) -> offset (0, +27) respecto al centro (233, 233)
    img_pilot = lv_image_create(scr);
    lv_image_set_src(img_pilot, &icon_chevron_pilot);
    lv_obj_align(img_pilot, LV_ALIGN_CENTER, 0, 27);

    // B. Zona Inferior: Ícono de Maniobra + Distancia Apilada (Y: 295..395)
    img_maniobra = lv_image_create(scr);
    lv_image_set_src(img_maniobra, &icon_straight);
    lv_obj_align(img_maniobra, LV_ALIGN_CENTER, -55, 110);

    lbl_distancia_num = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_distancia_num, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_distancia_num, lv_color_hex(0xEDEBEB), LV_PART_MAIN);
    lv_label_set_text(lbl_distancia_num, "0");
    lv_obj_align(lbl_distancia_num, LV_ALIGN_CENTER, 35, 96);

    lbl_distancia_unit = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_distancia_unit, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_distancia_unit, lv_color_hex(0xCCCCCC), LV_PART_MAIN);
    lv_label_set_text(lbl_distancia_unit, "m");
    lv_obj_align_to(lbl_distancia_unit, lbl_distancia_num, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);

    // C. Borde Curvo Inferior: Barra de Progreso (lv_arc 420x420, ángulos 55° a 125°)
    arc_progreso = lv_arc_create(scr);
    lv_obj_set_size(arc_progreso, 420, 420);
    lv_obj_align(arc_progreso, LV_ALIGN_CENTER, 0, 0);
    lv_arc_set_bg_angles(arc_progreso, 55, 125);
    lv_arc_set_range(arc_progreso, 0, 100);
    lv_arc_set_value(arc_progreso, 0);
    lv_obj_remove_style(arc_progreso, NULL, LV_PART_KNOB);
    lv_obj_set_clickable(arc_progreso, false);

    lv_obj_set_style_arc_width(arc_progreso, 5, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_progreso, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc_progreso, true, LV_PART_MAIN);

    lv_obj_set_style_arc_width(arc_progreso, 5, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc_progreso, lv_color_hex(0xEDEBEB), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc_progreso, true, LV_PART_INDICATOR);

    // D. Alerta de Radar: Sector medio-izquierdo a las 9 en punto (X: 85, Y: 223)
    badge_radar = lv_obj_create(scr);
    lv_obj_set_size(badge_radar, 84, 84);
    lv_obj_align(badge_radar, LV_ALIGN_CENTER, -148, -30);
    lv_obj_set_style_radius(badge_radar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(badge_radar, lv_color_hex(0xEDEBEB), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(badge_radar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(badge_radar, lv_color_hex(0xE51C23), LV_PART_MAIN);
    lv_obj_set_style_border_width(badge_radar, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(badge_radar, 0, LV_PART_MAIN);
    lv_obj_set_scrollable(badge_radar, false);
    lv_obj_set_clickable(badge_radar, false);

    lbl_radar_val = lv_label_create(badge_radar);
    lv_obj_set_style_text_font(lbl_radar_val, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_radar_val, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_label_set_text(lbl_radar_val, "60");
    lv_obj_center(lbl_radar_val);

    lbl_radar_dist = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_radar_dist, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_radar_dist, lv_color_hex(0xEDEBEB), LV_PART_MAIN);
    lv_label_set_text(lbl_radar_dist, "0 m");
    lv_obj_align_to(lbl_radar_dist, badge_radar, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);

    set_obj_visible(badge_radar, false);
    set_obj_visible(lbl_radar_dist, false);

    ui_gps_apply_state(
        initially_connected ? UI_STATE_CONNECTED_STANDBY : UI_STATE_DISCONNECTED,
        SCREEN_GPS_MAIN
    );
}

void ui_gps_update_navigation(const TelemetryData &telemetry, const lv_point_precise_t *raw_route_points) {
    current_telemetry = telemetry;
    if (current_telemetry.numPuntosRuta > 0 && raw_route_points != nullptr) {
        project_route_points(raw_route_points, current_telemetry.numPuntosRuta, current_route_points);
    }

    if (route_line != nullptr) {
        lv_line_set_points(route_line, current_route_points, current_telemetry.numPuntosRuta);
    }

    if (img_maniobra != nullptr) {
        lv_image_set_src(img_maniobra, get_maneuver_icon(current_telemetry.tipoManiobra));
    }

    if (lbl_distancia_num != nullptr && lbl_distancia_unit != nullptr) {
        int dist_m = current_telemetry.distanciaManiobra;
        if (dist_m < 0) dist_m = 0;

        if (dist_m >= 1000) {
            int km_int = dist_m / 1000;
            int km_dec = (dist_m % 1000) / 100;
            lv_label_set_text_fmt(lbl_distancia_num, "%d.%d", km_int, km_dec);
            lv_label_set_text(lbl_distancia_unit, "km");
        } else {
            lv_label_set_text_fmt(lbl_distancia_num, "%d", dist_m);
            lv_label_set_text(lbl_distancia_unit, "m");
        }
        lv_obj_align_to(lbl_distancia_unit, lbl_distancia_num, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);
    }

    if (arc_progreso != nullptr) {
        int prog = current_telemetry.progresoViaje;
        if (prog < 0) prog = 0;
        if (prog > 100) prog = 100;
        lv_arc_set_value(arc_progreso, prog);
    }

    if (badge_radar != nullptr && lbl_radar_val != nullptr && lbl_radar_dist != nullptr) {
        int limite = current_telemetry.limiteVelocidad;
        int dist_radar = current_telemetry.distanciaRadar;
        if (dist_radar < 0) dist_radar = 0;

        if (limite > 0) {
            lv_obj_set_style_text_font(lbl_radar_val, &lv_font_montserrat_28, LV_PART_MAIN);
            lv_label_set_text_fmt(lbl_radar_val, "%d", limite);
            lv_obj_center(lbl_radar_val);

            lv_label_set_text_fmt(lbl_radar_dist, "%d m", dist_radar);
            lv_obj_align_to(lbl_radar_dist, badge_radar, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);

            set_obj_visible(badge_radar, true);
            set_obj_visible(lbl_radar_dist, true);
        } else if (limite == -1) {
            lv_obj_set_style_text_font(lbl_radar_val, &lv_font_montserrat_16, LV_PART_MAIN);
            lv_label_set_text(lbl_radar_val, "RADAR");
            lv_obj_center(lbl_radar_val);

            lv_label_set_text_fmt(lbl_radar_dist, "%d m", dist_radar);
            lv_obj_align_to(lbl_radar_dist, badge_radar, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);

            set_obj_visible(badge_radar, true);
            set_obj_visible(lbl_radar_dist, true);
        } else {
            set_obj_visible(badge_radar, false);
            set_obj_visible(lbl_radar_dist, false);
        }
    }
}

void ui_gps_clear_navigation() {
    current_telemetry = TelemetryData();
    if (route_line != nullptr) {
        lv_line_set_points(route_line, current_route_points, 0);
    }
    if (arc_progreso != nullptr) {
        lv_arc_set_value(arc_progreso, 0);
    }
    set_obj_visible(badge_radar, false);
    set_obj_visible(lbl_radar_dist, false);
}
