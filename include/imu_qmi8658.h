#ifndef ZAMOSA_IMU_QMI8658_H
#define ZAMOSA_IMU_QMI8658_H

#include <Arduino.h>
#include "board_config.h"

// Estado procesado de la IMU QMI8658 (sin dependencias de LVGL)
struct ImuTelemetryState {
    float lean_angle_deg = 0.0f;
    int signed_lean_deg = 0;
    int display_lean_deg = 0;
    int max_left_deg = 0;
    int max_right_deg = 0;
    float ax_g = 0.0f;
    float ay_g = 0.0f;
    float az_g = 0.0f;
};

void init_qmi8658();
void qmi8658_set_active(bool active);
void imu_reset_filter_seed(uint32_t now_ms);

// Muestrea la IMU a 30 Hz (cada 33 ms) y actualiza el filtro complementario.
// Devuelve true si se procesó una nueva muestra y se actualizó out_state.
bool imu_poll_and_compute(uint32_t now_ms, ImuTelemetryState *out_state);

#endif // ZAMOSA_IMU_QMI8658_H
