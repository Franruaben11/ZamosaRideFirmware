#ifndef ZAMOSA_UI_TELEMETRY_H
#define ZAMOSA_UI_TELEMETRY_H

#include <Arduino.h>
#include <lvgl.h>
#include "imu_qmi8658.h"

void ui_telemetry_init();
lv_obj_t *ui_telemetry_get_screen();
void ui_telemetry_update(const ImuTelemetryState &imu_state);

#endif // ZAMOSA_UI_TELEMETRY_H
