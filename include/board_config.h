#ifndef ZAMOSA_BOARD_CONFIG_H
#define ZAMOSA_BOARD_CONFIG_H

#include <Arduino.h>

// ============================================================================
// 1. Pines I2C, QSPI, Pantalla AMOLED (Waveshare 1.75" 466x466) y Botón BOOT
// ============================================================================
#define IIC_SDA      15
#define IIC_SCL      14
#define TCA9554_ADDR 0x20
#define AXP2101_ADDR 0x34

#define LCD_CS       12
#define LCD_SCLK     38
#define LCD_SDIO0    4
#define LCD_SDIO1    5
#define LCD_SDIO2    6
#define LCD_SDIO3    7
#define LCD_RST      -1  // Pulso de reset por TCA9554 + GPIO 1 + GPIO 39

#define LCD_WIDTH    466
#define LCD_HEIGHT   466

#define BOOT_BUTTON_PIN      0
#define BOOT_BTN_COOLDOWN_MS 250U

// ============================================================================
// 2. Configuración BLE y Tiempos de Sistema
// ============================================================================
#define SERVICE_UUID                "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID         "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define BLE_MTU_SIZE                512
#define MAX_ROUTE_POINTS            10
#define BLE_DISCONNECT_GRACE_MS     3000U
#define DISCONNECTED_DISPLAY_OFF_MS 180000U
#define SECONDS_PER_DAY             86400U

// ============================================================================
// 3. Configuración IMU QMI8658C (Bus I2C compartido GPIO 15/14)
// ============================================================================
#define QMI8658_ADDR_PRIMARY 0x6B
#define QMI8658_ADDR_ALT     0x6A
#define QMI8658_REG_WHO_AM_I 0x00
#define QMI8658_REG_CTRL1    0x02
#define QMI8658_REG_CTRL2    0x03
#define QMI8658_REG_CTRL3    0x04
#define QMI8658_REG_CTRL5    0x06
#define QMI8658_REG_CTRL7    0x08
#define QMI8658_REG_AX_L     0x35

// Ajustes de orientación para montaje vertical frente al piloto:
// - Cambiar IMU_AY_SIGN a (-1.0f) si Izquierda y Derecha están invertidas
// - Cambiar IMU_AX_SIGN a (-1.0f) si el eje vertical X está invertido
// - Cambiar IMU_OMEGA_ROLL_SIGN a (-1.0f) si el giroscopio 3D empuja en sentido opuesto al inicio del giro
#define IMU_AX_SIGN          (1.0f)
#define IMU_AY_SIGN          (-1.0f)
#define IMU_GZ_SIGN          (1.0f)
#define IMU_OMEGA_ROLL_SIGN  (-1.0f)
#define IMU_SAMPLE_PERIOD_MS 33U

// ============================================================================
// 4. Enumeraciones Globales de Estado UI y Pantallas
// ============================================================================
enum UiState {
    UI_STATE_DISCONNECTED = 0,
    UI_STATE_CONNECTED_STANDBY = 1,
    UI_STATE_NAVIGATING = 2
};

enum ActiveScreen {
    SCREEN_GPS_MAIN = 0,
    SCREEN_TELEMETRY = 1
};

#endif // ZAMOSA_BOARD_CONFIG_H
