#ifndef ZAMOSA_DISPLAY_HAL_H
#define ZAMOSA_DISPLAY_HAL_H

#include <Arduino.h>
#include <cstddef>
#include <cstdint>

// Helpers de I2C expuestos para PMIC AXP2101, TCA9554 e IMU QMI8658
void i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t val);
uint8_t i2c_read_reg(uint8_t addr, uint8_t reg);
bool i2c_read_bytes(uint8_t addr, uint8_t reg, uint8_t *buf, size_t len);

// Inicialización de energía, controlador QSPI CO5300 y buffers de LVGL
void init_display_and_lvgl();

// Control de encendido y brillo del panel AMOLED
void display_turn_on();
void display_turn_off();
void display_set_brightness(uint8_t brightness);
bool display_is_panel_off();

#endif // ZAMOSA_DISPLAY_HAL_H
