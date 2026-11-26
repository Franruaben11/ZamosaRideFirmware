#include "imu_qmi8658.h"
#include <cmath>
#include "display_hal.h"

static uint8_t qmi8658_addr = 0;
static bool qmi8658_available = false;
static bool imu_filter_seeded = false;
static uint32_t last_imu_ms = 0;
static ImuTelemetryState imu_state;

void qmi8658_set_active(bool active) {
    if (!qmi8658_available) return;
    // CTRL7 (0x08): 0x03 activa acelerómetro + giroscopio; 0x00 los pone en reposo
    i2c_write_reg(qmi8658_addr, QMI8658_REG_CTRL7, active ? 0x03 : 0x00);
}

void imu_reset_filter_seed(uint32_t now_ms) {
    imu_filter_seeded = false;
    last_imu_ms = now_ms;
}

void init_qmi8658() {
    const uint8_t candidates[2] = {QMI8658_ADDR_PRIMARY, QMI8658_ADDR_ALT};
    for (size_t i = 0; i < 2; ++i) {
        uint8_t who = i2c_read_reg(candidates[i], QMI8658_REG_WHO_AM_I);
        if (who == 0x05) {
            qmi8658_addr = candidates[i];
            qmi8658_available = true;
            break;
        }
    }

    if (!qmi8658_available) {
        Serial.println("[IMU] QMI8658 no detectada en el bus I2C (0x6B / 0x6A)");
        return;
    }

    // Configurar interfaz e inicializar en reposo (CTRL7 = 0x00) hasta entrar a SCREEN_TELEMETRY
    i2c_write_reg(qmi8658_addr, QMI8658_REG_CTRL7, 0x00);
    i2c_write_reg(qmi8658_addr, QMI8658_REG_CTRL1, 0x40); // Auto-incremento de dirección, Little-Endian
    i2c_write_reg(qmi8658_addr, QMI8658_REG_CTRL2, 0x15); // Acelerómetro: +-4g (8192 LSB/g), ODR ~250 Hz
    i2c_write_reg(qmi8658_addr, QMI8658_REG_CTRL3, 0x55); // Giroscopio: +-512 dps (64 LSB/dps), ODR ~250 Hz
    i2c_write_reg(qmi8658_addr, QMI8658_REG_CTRL5, 0x11); // Habilitar LPF interno para Accel y Gyro

    Serial.printf("[IMU] QMI8658 inicializada en 0x%02X (en reposo CTRL7=0x00 hasta abrir TELEMETRIA)\n", qmi8658_addr);
}

bool imu_poll_and_compute(uint32_t now_ms, ImuTelemetryState *out_state) {
    if (!qmi8658_available) return false;

    uint32_t elapsed_ms = (uint32_t)(now_ms - last_imu_ms);
    if (elapsed_ms < IMU_SAMPLE_PERIOD_MS) return false;
    last_imu_ms = now_ms;

    uint8_t raw_buf[12];
    if (!i2c_read_bytes(qmi8658_addr, QMI8658_REG_AX_L, raw_buf, sizeof(raw_buf))) {
        return false;
    }

    int16_t raw_ax = (int16_t)(((uint16_t)raw_buf[1] << 8) | raw_buf[0]);
    int16_t raw_ay = (int16_t)(((uint16_t)raw_buf[3] << 8) | raw_buf[2]);
    int16_t raw_az = (int16_t)(((uint16_t)raw_buf[5] << 8) | raw_buf[4]);
    int16_t raw_gx = (int16_t)(((uint16_t)raw_buf[7] << 8) | raw_buf[6]);
    int16_t raw_gz = (int16_t)(((uint16_t)raw_buf[11] << 8) | raw_buf[10]);

    // Escalas: +-4g -> 8192 LSB/g; +-512 dps -> 64 LSB/(deg/s)
    float ax_g   = ((float)raw_ax / 8192.0f) * IMU_AX_SIGN;
    float ay_g   = ((float)raw_ay / 8192.0f) * IMU_AY_SIGN;
    float az_g   = (float)raw_az / 8192.0f;
    float gx_dps = (float)raw_gx / 64.0f;
    float gz_dps = ((float)raw_gz / 64.0f) * IMU_GZ_SIGN;

    imu_state.ax_g = ax_g;
    imu_state.ay_g = ay_g;
    imu_state.az_g = az_g;

    // En la Waveshare 1.75" montada vertical frente al piloto:
    // - El eje X del QMI8658 (ax_g) queda vertical (gravedad ~1g en reposo).
    // - El eje Y del QMI8658 (ay_g) queda horizontal/lateral (~0g en reposo, cambia al inclinar a izq/der).
    // Usamos hypotf(ax_g, az_g) como referencia vertical positiva para que en reposo marque siempre 0°
    // (tanto vertical a 90° como inclinada en el soporte del manubrio) y vaya de 0° a 90° hacia ambos lados.
    float vertical_ref = hypotf(ax_g, az_g);
    float accel_angle = atan2f(ay_g, vertical_ref) * (180.0f / (float)M_PI);

    if (!imu_filter_seeded) {
        // Inicializar directamente con accel_angle en la primera muestra tras activar SCREEN_TELEMETRY
        imu_state.lean_angle_deg = accel_angle;
        imu_filter_seeded = true;
    } else {
        float dt = (float)elapsed_ms * 0.001f;
        if (dt > 0.15f) dt = 0.033f; // Acotar dt por seguridad

        // Proyección 3D de la velocidad angular de roll (combina gx y gz según el ángulo de montaje)
        float omega_roll = 0.0f;
        if (vertical_ref > 0.05f) {
            omega_roll = ((az_g * gx_dps - ax_g * gz_dps) / vertical_ref) * IMU_OMEGA_ROLL_SIGN;
        }

        imu_state.lean_angle_deg = 0.80f * (imu_state.lean_angle_deg + omega_roll * dt) + 0.20f * accel_angle;

        int signed_deg = (int)lroundf(imu_state.lean_angle_deg);
        if (signed_deg < 0) {
            int left_mag = -signed_deg;
            if (left_mag > 90) left_mag = 90;
            if (left_mag > imu_state.max_left_deg) {
                imu_state.max_left_deg = left_mag;
            }
        } else if (signed_deg > 0) {
            int right_mag = signed_deg;
            if (right_mag > 90) right_mag = 90;
            if (right_mag > imu_state.max_right_deg) {
                imu_state.max_right_deg = right_mag;
            }
        }
    }

    int signed_deg = (int)lroundf(imu_state.lean_angle_deg);
    int display_deg = abs(signed_deg);
    if (display_deg > 180) display_deg = 180;

    imu_state.signed_lean_deg = signed_deg;
    imu_state.display_lean_deg = display_deg;

    if (out_state != nullptr) {
        *out_state = imu_state;
    }
    return true;
}
