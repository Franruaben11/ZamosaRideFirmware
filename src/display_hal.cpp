#include "display_hal.h"
#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include "board_config.h"

// Inicialización del bus QSPI y del controlador AMOLED (CO5300 / SH8601 QSPI)
static Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3
);

// Arduino_CO5300 envía el comando QSPI 0xC4=0x80 requerido por la Waveshare 1.75" (466x466)
static Arduino_OLED *gfx = new Arduino_CO5300(
    bus,
    LCD_RST,
    0,          // Rotación (0..3)
    LCD_WIDTH,  // Ancho (466)
    LCD_HEIGHT, // Alto (466)
    6,          // col_offset1 (centrado del panel 466px en RAM de 480px)
    0,          // row_offset1
    6,          // col_offset2
    0           // row_offset2
);

// Usamos doble buffer de 1/10 de pantalla en PSRAM para evitar tearing
#define DRAW_BUF_LINES (LCD_HEIGHT / 10)
#define DRAW_BUF_SIZE  (LCD_WIDTH * DRAW_BUF_LINES * sizeof(uint16_t))

static uint8_t *lv_buf1 = nullptr;
static uint8_t *lv_buf2 = nullptr;
static lv_display_t *disp = nullptr;
static bool panel_is_off = false;

void i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t val) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

uint8_t i2c_read_reg(uint8_t addr, uint8_t reg) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return 0;
    if (Wire.requestFrom(addr, (uint8_t)1) != 1) return 0;
    return Wire.read();
}

bool i2c_read_bytes(uint8_t addr, uint8_t reg, uint8_t *buf, size_t len) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(addr, (uint8_t)len) != len) return false;
    for (size_t i = 0; i < len; ++i) {
        buf[i] = Wire.read();
    }
    return true;
}

static void my_disp_flush(lv_display_t *display, const lv_area_t *area, uint8_t *px_map) {
    uint32_t w = lv_area_get_width(area);
    uint32_t h = lv_area_get_height(area);

    // draw16bitRGBBitmap usa writePixels(), que copia desde PSRAM al buffer interno MALLOC_CAP_DMA
    // del driver QSPI e invierte los bytes RGB565 automáticamente.
    gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)px_map, w, h);

    lv_display_flush_ready(display);
}

static void my_rounder_cb(lv_event_t *e) {
    lv_area_t *area = (lv_area_t *)lv_event_get_param(e);
    area->x1 = area->x1 & ~1;
    area->y1 = area->y1 & ~1;
    area->x2 = (area->x2 & ~1) + 1;
    area->y2 = (area->y2 & ~1) + 1;
}

static uint32_t my_tick_get_cb(void) {
    return millis();
}

static void init_board_power_and_reset() {
    Wire.begin(IIC_SDA, IIC_SCL);
    Wire.setClock(400000);
    delay(20);

    // 1. Habilitar rieles de alimentación LDO del PMIC AXP2101 (0x34) a 3.3V (0x1C = (3300-500)/100)
    // Esto alimenta el panel AMOLED y periféricos en la Waveshare 1.75"
    i2c_write_reg(AXP2101_ADDR, 0x92, 0x1C); // ALDO1 = 3.3V
    i2c_write_reg(AXP2101_ADDR, 0x93, 0x1C); // ALDO2 = 3.3V
    i2c_write_reg(AXP2101_ADDR, 0x94, 0x1C); // ALDO3 = 3.3V
    i2c_write_reg(AXP2101_ADDR, 0x95, 0x1C); // ALDO4 = 3.3V
    i2c_write_reg(AXP2101_ADDR, 0x96, 0x1C); // BLDO1 = 3.3V
    i2c_write_reg(AXP2101_ADDR, 0x97, 0x1C); // BLDO2 = 3.3V
    i2c_write_reg(AXP2101_ADDR, 0x99, 0x1C); // DLDO1 = 3.3V
    uint8_t ldo_en = i2c_read_reg(AXP2101_ADDR, 0x90);
    i2c_write_reg(AXP2101_ADDR, 0x90, ldo_en | 0xBF); // Encender ALDO1..4, BLDO1..2, DLDO1
    delay(30);

    // 2. Configurar EXIO0..EXIO2 como salidas en TCA9554 (0x20) y hacer pulso de reset
    i2c_write_reg(TCA9554_ADDR, 0x03, 0xF8);
    i2c_write_reg(TCA9554_ADDR, 0x01, 0x00);

    // 3. Pulso de reset también en GPIO 1 y GPIO 39 (pines de LCD_RST según revisión de placa)
    pinMode(1, OUTPUT);
    pinMode(39, OUTPUT);
    digitalWrite(1, LOW);
    digitalWrite(39, LOW);
    delay(50);

    i2c_write_reg(TCA9554_ADDR, 0x01, 0x07);
    digitalWrite(1, HIGH);
    digitalWrite(39, HIGH);
    delay(150);
}

void init_display_and_lvgl() {
    init_board_power_and_reset();

    if (!gfx->begin()) {
        Serial.println("[DISPLAY] Error al inicializar el controlador AMOLED");
    }
    gfx->displayOn();
    panel_is_off = false;
    gfx->setBrightness(255);
    gfx->fillScreen(0x0000);

    lv_init();
    lv_tick_set_cb(my_tick_get_cb);

    // Reservar buffers de renderizado en PSRAM si está disponible;
    // si PSRAM no está activa, usar un buffer más liviano en SRAM interna para no agotar el heap junto a BLE
    uint32_t buf_size = DRAW_BUF_SIZE;
    if (psramFound()) {
        Serial.println("[MEM] PSRAM detectada: alocando doble buffer LVGL en PSRAM");
        lv_buf1 = (uint8_t *)ps_malloc(buf_size);
        lv_buf2 = (uint8_t *)ps_malloc(buf_size);
    } else {
        buf_size = LCD_WIDTH * 20 * sizeof(uint16_t);
        Serial.println("[MEM] Sin PSRAM: alocando buffer LVGL reducido en SRAM interna");
        lv_buf1 = (uint8_t *)malloc(buf_size);
        lv_buf2 = nullptr;
    }

    disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
    lv_display_set_flush_cb(disp, my_disp_flush);
    lv_display_set_buffers(disp, lv_buf1, lv_buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_add_event_cb(disp, my_rounder_cb, LV_EVENT_INVALIDATE_AREA, nullptr);

    // Fondo negro absoluto (#000000) para apagar píxeles AMOLED en reposo
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, LV_PART_MAIN);
}

void display_turn_on() {
    gfx->displayOn();
    panel_is_off = false;
}

void display_turn_off() {
    gfx->displayOff();
    panel_is_off = true;
}

void display_set_brightness(uint8_t brightness) {
    gfx->setBrightness(brightness);
}

bool display_is_panel_off() {
    return panel_is_off;
}
