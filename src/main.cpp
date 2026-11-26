#include <Arduino.h>
#include <lvgl.h>
#include "board_config.h"
#include "display_hal.h"
#include "ble_service.h"
#include "imu_qmi8658.h"
#include "ui_gps.h"
#include "ui_telemetry.h"

static TaskHandle_t mainTaskHandle = nullptr;
static ActiveScreen current_screen = SCREEN_GPS_MAIN;

// Estado de botón físico BOOT (GPIO 0) con ISR + Cooldown no bloqueante
static volatile bool boot_btn_irq_triggered = false;
static bool boot_btn_armed = true;
static uint32_t boot_btn_last_trigger_ms = 0;

void IRAM_ATTR on_boot_btn_isr() {
    boot_btn_irq_triggered = true;
    if (mainTaskHandle != nullptr) {
        BaseType_t hpw = pdFALSE;
        vTaskNotifyGiveFromISR(mainTaskHandle, &hpw);
        portYIELD_FROM_ISR(hpw);
    }
}

static void handle_boot_button() {
    uint32_t now_ms = millis();
    bool is_low = (digitalRead(BOOT_BUTTON_PIN) == LOW);
    bool irq_fired = boot_btn_irq_triggered;
    if (irq_fired) {
        boot_btn_irq_triggered = false;
    }

    // Rearmar únicamente cuando el botón fue soltado (HIGH) y pasó el tiempo mínimo de rebote
    if (!is_low) {
        if (!boot_btn_armed && (uint32_t)(now_ms - boot_btn_last_trigger_ms) >= 50U) {
            boot_btn_armed = true;
        }
        return;
    }

    // Disparar inmediatamente en el primer flanco LOW válido con cooldown de 250 ms
    if (boot_btn_armed && (uint32_t)(now_ms - boot_btn_last_trigger_ms) >= BOOT_BTN_COOLDOWN_MS) {
        boot_btn_last_trigger_ms = now_ms;
        boot_btn_armed = false;

        UiState ui_state = ui_gps_get_state();

        // Si el panel AMOLED estaba auto-apagado por inactividad, la pulsación lo despierta primero
        if (display_is_panel_off()) {
            display_turn_on();
            ui_gps_reset_disconnected_timer(now_ms);
            uint8_t target_brightness = (current_screen == SCREEN_TELEMETRY || ui_state == UI_STATE_NAVIGATING)
                                            ? 255
                                            : (ui_state == UI_STATE_CONNECTED_STANDBY ? 160 : 60);
            display_set_brightness(target_brightness);
            Serial.println("[BTN] Panel AMOLED encendido por botón BOOT");
            return;
        }

        // Alternar entre la pantalla GPS/Standby y la pantalla de Telemetría sin destruir objetos (auto_del = false)
        if (current_screen == SCREEN_GPS_MAIN) {
            current_screen = SCREEN_TELEMETRY;
            setCpuFrequencyMhz(ui_state == UI_STATE_NAVIGATING ? 160 : 80);
            qmi8658_set_active(true);
            imu_reset_filter_seed(now_ms);
            display_turn_on();
            display_set_brightness(255);
            lv_screen_load_anim(ui_telemetry_get_screen(), LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
            Serial.println("[UI] Pantalla activa -> SCREEN_TELEMETRY (QMI8658 activa)");
        } else {
            current_screen = SCREEN_GPS_MAIN;
            qmi8658_set_active(false);
            ui_gps_reset_disconnected_timer(now_ms);
            ui_gps_apply_state(ui_state, current_screen);
            lv_screen_load_anim(ui_gps_get_screen(), LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
            Serial.println("[UI] Pantalla activa -> SCREEN_GPS_MAIN (QMI8658 en reposo)");
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("=== Iniciando ZamosaRide Firmware (ESP32-S3) ===");

    mainTaskHandle = xTaskGetCurrentTaskHandle();

    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BOOT_BUTTON_PIN), on_boot_btn_isr, FALLING);

    init_ble(mainTaskHandle);
    init_display_and_lvgl();
    init_qmi8658();

    ui_gps_init(ble_is_client_connected());
    ui_telemetry_init();
}

void loop() {
    handle_boot_button();

    bool is_connected = false;
    uint32_t disconnected_since = 0;
    TelemetryData latest_telemetry;
    lv_point_precise_t raw_route_points[MAX_ROUTE_POINTS];

    bool has_new_telemetry = ble_fetch_state(
        &is_connected,
        &disconnected_since,
        &latest_telemetry,
        raw_route_points
    );

    // Si el panel estaba apagado por inactividad, encenderlo inmediatamente ante onConnect o onWrite
    if (display_is_panel_off() && (is_connected || has_new_telemetry)) {
        display_turn_on();
        display_set_brightness(60);
        ui_gps_reset_disconnected_timer(millis());
    }

    // 1. Transición inmediata al conectar BLE desde estado desconectado
    if (is_connected && ui_gps_get_state() == UI_STATE_DISCONNECTED) {
        ui_gps_apply_state(UI_STATE_CONNECTED_STANDBY, current_screen);
    }

    // 2. Procesamiento de paquetes JSON entrantes (Navegación activa vs Cierre/Sincronización)
    if (has_new_telemetry) {
        bool route_is_active =
            (latest_telemetry.distanciaDestino > 0) || (latest_telemetry.numPuntosRuta > 0);

        if (route_is_active) {
            if (ui_gps_get_state() != UI_STATE_NAVIGATING) {
                ui_gps_apply_state(UI_STATE_NAVIGATING, current_screen);
            }
            ui_gps_update_navigation(latest_telemetry, raw_route_points);
        } else {
            ui_gps_clear_navigation();
            if (is_connected && ui_gps_get_state() != UI_STATE_CONNECTED_STANDBY) {
                ui_gps_apply_state(UI_STATE_CONNECTED_STANDBY, current_screen);
            }
        }
    }

    // 3. Si el enlace BLE cae y permanece desconectado por más de 3 segundos -> UI_STATE_DISCONNECTED
    if (!is_connected && ui_gps_get_state() != UI_STATE_DISCONNECTED) {
        uint32_t elapsed_disc = (uint32_t)(millis() - disconnected_since);
        if (elapsed_disc >= BLE_DISCONNECT_GRACE_MS) {
            ui_gps_clear_navigation();
            ui_gps_apply_state(UI_STATE_DISCONNECTED, current_screen);
        }
    }

    // 4. Auto-apagado del panel AMOLED tras 3 minutos continuos en UI_STATE_DISCONNECTED (solo en pantalla GPS/Standby)
    if (current_screen == SCREEN_GPS_MAIN &&
        ui_gps_get_state() == UI_STATE_DISCONNECTED &&
        !is_connected &&
        !display_is_panel_off()) {
        uint32_t elapsed_in_disc = (uint32_t)(millis() - ui_gps_get_disconnected_entered_ms());
        if (elapsed_in_disc >= DISCONNECTED_DISPLAY_OFF_MS) {
            display_turn_off();
        }
    }

    // 5. Actualizar reloj local en Standby Conectado únicamente cuando cambie el minuto
    if (ui_gps_get_state() == UI_STATE_CONNECTED_STANDBY) {
        ui_gps_update_standby_clock();
    }

    // 6. Muestreo de IMU (30 Hz / 33 ms) y filtro complementario exclusivamente en SCREEN_TELEMETRY
    if (current_screen == SCREEN_TELEMETRY) {
        ImuTelemetryState imu_state;
        if (imu_poll_and_compute(millis(), &imu_state)) {
            ui_telemetry_update(imu_state);
        }
    }

    uint32_t time_till_next = lv_timer_handler();

    if (!boot_btn_armed) {
        // Evitar dormir 500 ms mientras el botón sigue presionado para rearmar apenas vuelva a HIGH
        uint32_t wait_ms = min(time_till_next, (uint32_t)20);
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait_ms));
    } else if (ui_gps_get_state() == UI_STATE_NAVIGATING) {
        uint32_t wait_ms = min(time_till_next, (uint32_t)16);
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait_ms));
    } else if (current_screen == SCREEN_TELEMETRY) {
        uint32_t wait_ms = min(time_till_next, (uint32_t)IMU_SAMPLE_PERIOD_MS);
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait_ms));
    } else {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));
    }
}
