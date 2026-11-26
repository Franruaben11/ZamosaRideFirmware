#include "ui_telemetry.h"
#include <cmath>

static lv_obj_t *scr_telemetry = nullptr;

// Widgets: Pantalla de Telemetría (Inclinómetro + Arco Superior + Picos MAX IZQ / MAX DER)
static lv_obj_t *arc_telem_gauge         = nullptr;
static lv_obj_t *line_telem_needle       = nullptr;
static lv_point_precise_t telem_needle_points[2];
static int last_rendered_needle_deg      = -999;

static lv_obj_t *lbl_telem_title         = nullptr;
static lv_obj_t *lbl_telem_angle         = nullptr;
static lv_obj_t *lbl_telem_max_left_hdr  = nullptr;
static lv_obj_t *lbl_telem_max_left_val  = nullptr;
static lv_obj_t *lbl_telem_max_right_hdr = nullptr;
static lv_obj_t *lbl_telem_max_right_val = nullptr;

static int last_rendered_lean_deg      = -999;
static int last_rendered_max_left_deg  = -1;
static int last_rendered_max_right_deg = -1;

static void update_telem_needle_line(int signed_deg) {
    if (line_telem_needle == nullptr) return;

    int clamped_deg = signed_deg;
    if (clamped_deg < -60) clamped_deg = -60;
    if (clamped_deg > 60)  clamped_deg = 60;

    if (clamped_deg == last_rendered_needle_deg) return;
    last_rendered_needle_deg = clamped_deg;

    float rad   = (float)clamped_deg * ((float)M_PI / 180.0f);
    float sin_a = sinf(rad);
    float cos_a = cosf(rad);

    // Arco de 430x430 (R_ext = 215, grosor = 38 -> R_int = 177). Con línea de 8 px redondeada (+/-4 px),
    // los centros de los extremos van de R=181 a R=211 para calzar exacto dentro de la pista gris.
    const float cx      = 233.0f;
    const float cy      = 233.0f;
    const float r_inner = 181.0f;
    const float r_outer = 211.0f;

    telem_needle_points[0].x = (lv_value_precise_t)lroundf(cx + r_inner * sin_a);
    telem_needle_points[0].y = (lv_value_precise_t)lroundf(cy - r_inner * cos_a);
    telem_needle_points[1].x = (lv_value_precise_t)lroundf(cx + r_outer * sin_a);
    telem_needle_points[1].y = (lv_value_precise_t)lroundf(cy - r_outer * cos_a);

    lv_line_set_points(line_telem_needle, telem_needle_points, 2);
}

lv_obj_t *ui_telemetry_get_screen() {
    return scr_telemetry;
}

void ui_telemetry_init() {
    scr_telemetry = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr_telemetry, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr_telemetry, LV_OPA_COVER, LV_PART_MAIN);

    // Arco superior de referencia (-60° a +60° respecto a las 12 en punto -> 210° a 330° en LVGL)
    arc_telem_gauge = lv_arc_create(scr_telemetry);
    lv_obj_set_size(arc_telem_gauge, 430, 430);
    lv_obj_align(arc_telem_gauge, LV_ALIGN_CENTER, 0, 0);
    lv_arc_set_bg_angles(arc_telem_gauge, 210, 330);
    lv_obj_remove_style(arc_telem_gauge, NULL, LV_PART_KNOB);
    lv_obj_set_clickable(arc_telem_gauge, false);

    lv_obj_set_style_arc_width(arc_telem_gauge, 38, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_telem_gauge, lv_color_hex(0x242424), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc_telem_gauge, true, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc_telem_gauge, LV_OPA_TRANSP, LV_PART_INDICATOR);

    // Línea radial indicadora de inclinación en tiempo real (8 px, #EDEBEB)
    line_telem_needle = lv_line_create(scr_telemetry);
    lv_obj_set_pos(line_telem_needle, 0, 0);
    lv_obj_set_style_line_width(line_telem_needle, 8, LV_PART_MAIN);
    lv_obj_set_style_line_color(line_telem_needle, lv_color_hex(0xEDEBEB), LV_PART_MAIN);
    lv_obj_set_style_line_rounded(line_telem_needle, true, LV_PART_MAIN);
    last_rendered_needle_deg = -999;
    update_telem_needle_line(0);

    lbl_telem_title = lv_label_create(scr_telemetry);
    lv_obj_set_style_text_font(lbl_telem_title, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_telem_title, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_label_set_text(lbl_telem_title, "INCLINACION");
    lv_obj_align(lbl_telem_title, LV_ALIGN_CENTER, 0, -64);

    lbl_telem_angle = lv_label_create(scr_telemetry);
    lv_obj_set_style_text_font(lbl_telem_angle, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_telem_angle, lv_color_hex(0xEDEBEB), LV_PART_MAIN);
    lv_label_set_text(lbl_telem_angle, "0°");
    lv_obj_align(lbl_telem_angle, LV_ALIGN_CENTER, 0, -8);

    // Columna izquierda: MAX IZQ
    lbl_telem_max_left_hdr = lv_label_create(scr_telemetry);
    lv_obj_set_style_text_font(lbl_telem_max_left_hdr, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_telem_max_left_hdr, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_label_set_text(lbl_telem_max_left_hdr, "MAX IZQ");
    lv_obj_align(lbl_telem_max_left_hdr, LV_ALIGN_CENTER, -135, -24);

    lbl_telem_max_left_val = lv_label_create(scr_telemetry);
    lv_obj_set_style_text_font(lbl_telem_max_left_val, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_telem_max_left_val, lv_color_hex(0xEDEBEB), LV_PART_MAIN);
    lv_label_set_text(lbl_telem_max_left_val, "0°");
    lv_obj_align(lbl_telem_max_left_val, LV_ALIGN_CENTER, -135, 12);

    // Columna derecha: MAX DER
    lbl_telem_max_right_hdr = lv_label_create(scr_telemetry);
    lv_obj_set_style_text_font(lbl_telem_max_right_hdr, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_telem_max_right_hdr, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_label_set_text(lbl_telem_max_right_hdr, "MAX DER");
    lv_obj_align(lbl_telem_max_right_hdr, LV_ALIGN_CENTER, 135, -24);

    lbl_telem_max_right_val = lv_label_create(scr_telemetry);
    lv_obj_set_style_text_font(lbl_telem_max_right_val, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_telem_max_right_val, lv_color_hex(0xEDEBEB), LV_PART_MAIN);
    lv_label_set_text(lbl_telem_max_right_val, "0°");
    lv_obj_align(lbl_telem_max_right_val, LV_ALIGN_CENTER, 135, 12);

    last_rendered_lean_deg = 0;
    last_rendered_max_left_deg = 0;
    last_rendered_max_right_deg = 0;
}

void ui_telemetry_update(const ImuTelemetryState &imu_state) {
    update_telem_needle_line(imu_state.signed_lean_deg);

    if (imu_state.display_lean_deg != last_rendered_lean_deg && lbl_telem_angle != nullptr) {
        last_rendered_lean_deg = imu_state.display_lean_deg;
        lv_label_set_text_fmt(lbl_telem_angle, "%d°", imu_state.display_lean_deg);
    }

    if (imu_state.max_left_deg != last_rendered_max_left_deg && lbl_telem_max_left_val != nullptr) {
        last_rendered_max_left_deg = imu_state.max_left_deg;
        lv_label_set_text_fmt(lbl_telem_max_left_val, "%d°", imu_state.max_left_deg);
    }

    if (imu_state.max_right_deg != last_rendered_max_right_deg && lbl_telem_max_right_val != nullptr) {
        last_rendered_max_right_deg = imu_state.max_right_deg;
        lv_label_set_text_fmt(lbl_telem_max_right_val, "%d°", imu_state.max_right_deg);
    }
}
