#ifndef ZAMOSA_UI_GPS_H
#define ZAMOSA_UI_GPS_H

#include <Arduino.h>
#include <lvgl.h>
#include "board_config.h"
#include "ble_service.h"

void ui_gps_init(bool initially_connected);
lv_obj_t *ui_gps_get_screen();

void ui_gps_apply_state(UiState new_state, ActiveScreen active_screen);
UiState ui_gps_get_state();

uint32_t ui_gps_get_disconnected_entered_ms();
void ui_gps_reset_disconnected_timer(uint32_t now_ms);

void ui_gps_update_navigation(const TelemetryData &telemetry, const lv_point_precise_t *raw_route_points);
void ui_gps_clear_navigation();
void ui_gps_update_standby_clock();

#endif // ZAMOSA_UI_GPS_H
