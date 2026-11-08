#include <Arduino.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include <NimBLEDevice.h>
#include <ArduinoJson.h>
#include <cstdio>

// En Arduino-ESP32 v3.3.x, initArduino() libera la memoria del controlador BLE (~36KB en 0x3fcef180)
// al heap general salvo que bleInUse() / btInUse() devuelvan true. Sin esto, NimBLE corrompe el heap al iniciar.
extern "C" bool btInUse(void) { return true; }
extern "C" bool bleInUse(void) { return true; }

// ============================================================================
// 1. Configuración de Pines QSPI, I2C y Pantalla (Waveshare 1.75" AMOLED)
// ============================================================================
#define IIC_SDA      15
#define IIC_SCL      14
#define TCA9554_ADDR 0x20
#define AXP2101_ADDR 0x34

#define LCD_CS    12
#define LCD_SCLK  38
#define LCD_SDIO0 4
#define LCD_SDIO1 5
#define LCD_SDIO2 6
#define LCD_SDIO3 7
#define LCD_RST   -1  // El pulso de reset se realiza en init_board_power_and_reset() (TCA9554 + GPIO 1 + GPIO 39)

#define LCD_WIDTH  466
#define LCD_HEIGHT 466

// Inicialización del bus QSPI y del controlador AMOLED (CO5300 / SH8601 QSPI)
Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3
);

// Arduino_CO5300 envía el comando QSPI 0xC4=0x80 requerido por la Waveshare 1.75" (466x466)
Arduino_OLED *gfx = new Arduino_CO5300(
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

// ============================================================================
// 2. Configuración de LVGL y Buffers de Pantalla (Doble Buffer en PSRAM)
// ============================================================================
// Usamos doble buffer de 1/10 de pantalla en PSRAM para evitar tearing
#define DRAW_BUF_LINES (LCD_HEIGHT / 10)
#define DRAW_BUF_SIZE  (LCD_WIDTH * DRAW_BUF_LINES * sizeof(uint16_t))

static uint8_t *lv_buf1 = nullptr;
static uint8_t *lv_buf2 = nullptr;
static lv_display_t *disp = nullptr;

// Callback de volcado (flush) hacia el controlador AMOLED por QSPI
void my_disp_flush(lv_display_t *display, const lv_area_t *area, uint8_t *px_map) {
    uint32_t w = lv_area_get_width(area);
    uint32_t h = lv_area_get_height(area);

    // draw16bitRGBBitmap usa writePixels(), que copia desde PSRAM al buffer interno MALLOC_CAP_DMA
    // del driver QSPI e invierte los bytes RGB565 automáticamente.
    gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)px_map, w, h);

    lv_display_flush_ready(display);
}

// Callback para alinear las ventanas de refresco parcial a coordenadas pares (requisito de AMOLED QSPI)
void my_rounder_cb(lv_event_t *e) {
    lv_area_t *area = (lv_area_t *)lv_event_get_param(e);
    area->x1 = area->x1 & ~1;
    area->y1 = area->y1 & ~1;
    area->x2 = (area->x2 & ~1) + 1;
    area->y2 = (area->y2 & ~1) + 1;
}

// Proveedor de tiempo para LVGL
static uint32_t my_tick_get_cb(void) {
    return millis();
}

// ============================================================================
// 3. Configuración BLE (NimBLE) y Estado Sincronizado (Thread-Safe)
// ============================================================================
#define SERVICE_UUID            "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID     "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define BLE_MTU_SIZE            512
#define MAX_ROUTE_POINTS        10
#define BLE_DISCONNECT_GRACE_MS 3000U
#define SECONDS_PER_DAY         86400U

enum UiState {
    UI_STATE_DISCONNECTED = 0,
    UI_STATE_CONNECTED_STANDBY = 1,
    UI_STATE_NAVIGATING = 2
};

struct TelemetryData {
    int tipoManiobra = 0;
    int distanciaManiobra = 0;
    int distanciaDestino = 0;
    int velocidadActual = 0;
    int progresoViaje = 0;
    size_t numPuntosRuta = 0;
};

static portMUX_TYPE state_mux = portMUX_INITIALIZER_UNLOCKED;

static TelemetryData shared_telemetry;
static lv_point_precise_t shared_route_points[MAX_ROUTE_POINTS];
static volatile bool new_telemetry_available = false;

static volatile bool ble_is_connected = false;
static volatile uint32_t ble_disconnected_since_ms = 0;

static volatile bool time_is_synced = false;
static volatile uint32_t synced_day_seconds = 0;
static volatile uint32_t synced_at_millis = 0;

// Copia local consumida exclusivamente en loop() (hilo de LVGL)
static TelemetryData current_telemetry;
static lv_point_precise_t current_route_points[MAX_ROUTE_POINTS];

static bool parse_time_string(const char *str, uint32_t *out_seconds) {
    if (str == nullptr || str[0] == '\0') return false;
    int hh = 0;
    int mm = 0;
    int ss = 0;
    int matched = sscanf(str, "%d:%d:%d", &hh, &mm, &ss);
    if (matched < 2) return false;
    if (hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 59) return false;
    *out_seconds = (uint32_t)(hh * 3600 + mm * 60 + ss);
    return true;
}

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *pServer, ble_gap_conn_desc *desc) override {
        portENTER_CRITICAL(&state_mux);
        ble_is_connected = true;
        portEXIT_CRITICAL(&state_mux);

        Serial.printf("[BLE] Cliente conectado (handle: %d)\n", desc->conn_handle);
        pServer->updateConnParams(desc->conn_handle, 12, 24, 0, 60);
    }

    void onDisconnect(NimBLEServer *pServer) override {
        uint32_t now_ms = millis();
        portENTER_CRITICAL(&state_mux);
        ble_is_connected = false;
        ble_disconnected_since_ms = now_ms;
        portEXIT_CRITICAL(&state_mux);

        Serial.println("[BLE] Cliente desconectado, reiniciando advertising...");
        NimBLEDevice::startAdvertising();
    }
};

class TelemetryCharCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *pCharacteristic) override {
        std::string rxValue = pCharacteristic->getValue();
        if (rxValue.empty()) {
            return;
        }

        StaticJsonDocument<768> doc;
        DeserializationError error = deserializeJson(doc, rxValue);

        if (error) {
            Serial.printf("[JSON] Error al deserializar: %s\n", error.c_str());
            return;
        }

        TelemetryData parsed;
        parsed.tipoManiobra      = doc["tipoManiobra"] | 0;
        parsed.distanciaManiobra = doc["distanciaManiobra"] | 0;
        parsed.distanciaDestino  = doc["distanciaDestino"] | 0;
        parsed.velocidadActual   = doc["velocidadActual"] | 0;
        parsed.progresoViaje     = doc["progresoViaje"] | 0;

        lv_point_precise_t parsed_points[MAX_ROUTE_POINTS];
        JsonArray puntos = doc["puntosRuta"].as<JsonArray>();
        size_t count = 0;
        if (!puntos.isNull()) {
            for (JsonArray pt : puntos) {
                if (count >= MAX_ROUTE_POINTS) break;
                if (pt.size() >= 2) {
                    parsed_points[count].x = pt[0].as<int>();
                    parsed_points[count].y = pt[1].as<int>();
                    count++;
                }
            }
        }
        parsed.numPuntosRuta = count;

        uint32_t parsed_day_seconds = 0;
        bool has_valid_time = false;
        if (doc["hora"].is<const char *>()) {
            has_valid_time = parse_time_string(doc["hora"].as<const char *>(), &parsed_day_seconds);
        } else if (doc["epoch"].is<uint32_t>()) {
            parsed_day_seconds = doc["epoch"].as<uint32_t>() % SECONDS_PER_DAY;
            has_valid_time = true;
        }

        uint32_t now_ms = millis();
        portENTER_CRITICAL(&state_mux);
        shared_telemetry = parsed;
        for (size_t i = 0; i < count; ++i) {
            shared_route_points[i] = parsed_points[i];
        }
        if (has_valid_time) {
            synced_day_seconds = parsed_day_seconds;
            synced_at_millis = now_ms;
            time_is_synced = true;
        }
        new_telemetry_available = true;
        portEXIT_CRITICAL(&state_mux);

        Serial.printf(
            "[BLE->JSON] Maniobra:%d | DistMan:%dm | DistDest:%dm | Vel:%dkm/h | Prog:%d%% | Puntos:%u | Hora:%s\n",
            parsed.tipoManiobra,
            parsed.distanciaManiobra,
            parsed.distanciaDestino,
            parsed.velocidadActual,
            parsed.progresoViaje,
            (unsigned int)parsed.numPuntosRuta,
            doc["hora"] | "--:--"
        );
    }
};

void init_ble() {
    NimBLEDevice::init("ZamosaRide-S3");
    NimBLEDevice::setMTU(BLE_MTU_SIZE);

    NimBLEServer *pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    NimBLEService *pService = pServer->createService(SERVICE_UUID);
    NimBLECharacteristic *pCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );

    pCharacteristic->setCallbacks(new TelemetryCharCallbacks());
    pService->start();

    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->start();

    Serial.println("[BLE] Servidor NimBLE iniciado (MTU: 512). Esperando conexión...");
}

// Escritura auxiliar por I2C para el PMIC AXP2101 (0x34) y el expansor TCA9554 (0x20)
static void i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t val) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

static uint8_t i2c_read_reg(uint8_t addr, uint8_t reg) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return 0;
    if (Wire.requestFrom(addr, (uint8_t)1) != 1) return 0;
    return Wire.read();
}

static void init_board_power_and_reset() {
    Wire.begin(IIC_SDA, IIC_SCL);
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

// ============================================================================
// 4. Objetos UI de LVGL (Standby Desconectado, Standby Conectado y Navegación)
// ============================================================================
static UiState current_ui_state = UI_STATE_DISCONNECTED;
static int last_rendered_minute = -2;

// Widgets: Modo Standby Desconectado
static lv_obj_t *lbl_disc_icon     = nullptr;
static lv_obj_t *lbl_disc_title    = nullptr;
static lv_obj_t *lbl_disc_subtitle = nullptr;
static lv_obj_t *lbl_disc_status   = nullptr;

// Widgets: Modo Standby Conectado
static lv_obj_t *lbl_conn_status = nullptr;
static lv_obj_t *lbl_conn_clock  = nullptr;
static lv_obj_t *lbl_conn_ready  = nullptr;

// Widgets: Modo Navegación Activa
static lv_obj_t *route_line    = nullptr;
static lv_obj_t *lbl_maniobra  = nullptr;
static lv_obj_t *lbl_velocidad = nullptr;
static lv_obj_t *lbl_destino   = nullptr;

static void set_obj_visible(lv_obj_t *obj, bool visible) {
    if (obj == nullptr) return;
    if (visible) {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static void update_standby_clock() {
    if (lbl_conn_clock == nullptr) return;

    bool synced = false;
    uint32_t base_seconds = 0;
    uint32_t base_millis = 0;

    portENTER_CRITICAL(&state_mux);
    synced = time_is_synced;
    base_seconds = synced_day_seconds;
    base_millis = synced_at_millis;
    portEXIT_CRITICAL(&state_mux);

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

static void apply_ui_state(UiState new_state) {
    current_ui_state = new_state;

    bool show_disc = (new_state == UI_STATE_DISCONNECTED);
    bool show_conn = (new_state == UI_STATE_CONNECTED_STANDBY);
    bool show_nav  = (new_state == UI_STATE_NAVIGATING);

    set_obj_visible(lbl_disc_icon, show_disc);
    set_obj_visible(lbl_disc_title, show_disc);
    set_obj_visible(lbl_disc_subtitle, show_disc);
    set_obj_visible(lbl_disc_status, show_disc);

    set_obj_visible(lbl_conn_status, show_conn);
    set_obj_visible(lbl_conn_clock, show_conn);
    set_obj_visible(lbl_conn_ready, show_conn);

    set_obj_visible(route_line, show_nav);
    set_obj_visible(lbl_maniobra, show_nav);
    set_obj_visible(lbl_velocidad, show_nav);
    set_obj_visible(lbl_destino, show_nav);

    if (show_conn) {
        last_rendered_minute = -2; // Forzar actualización inmediata del reloj al entrar a Standby Conectado
        update_standby_clock();
    }
}

void init_ui() {
    lv_obj_t *scr = lv_screen_active();

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
    lv_obj_set_style_text_color(lbl_disc_title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
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
    lv_obj_set_style_text_color(lbl_conn_clock, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_label_set_text(lbl_conn_clock, "--:--");
    lv_obj_align(lbl_conn_clock, LV_ALIGN_CENTER, 0, -6);

    lbl_conn_ready = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_conn_ready, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_conn_ready, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_label_set_text(lbl_conn_ready, "LISTO PARA NAVEGAR");
    lv_obj_align(lbl_conn_ready, LV_ALIGN_CENTER, 0, 56);

    // ------------------------------------------------------------------------
    // 3. Modo Navegación Activa (Intacto)
    // ------------------------------------------------------------------------
    route_line = lv_line_create(scr);
    lv_obj_set_pos(route_line, 0, 0);
    lv_obj_set_style_line_width(route_line, 14, LV_PART_MAIN);
    lv_obj_set_style_line_color(route_line, lv_color_hex(0x00E5FF), LV_PART_MAIN);
    lv_obj_set_style_line_rounded(route_line, true, LV_PART_MAIN);
    current_telemetry.numPuntosRuta = 0;
    lv_line_set_points(route_line, current_route_points, 0);

    // Etiqueta superior: Maniobra y distancia a maniobra
    lbl_maniobra = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl_maniobra, lv_color_white(), LV_PART_MAIN);
    lv_label_set_text(lbl_maniobra, "Maniobra: -- (0 m)");
    lv_obj_align(lbl_maniobra, LV_ALIGN_TOP_MID, 0, 28);

    // Etiqueta inferior izquierda/centro: Velocidad actual
    lbl_velocidad = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl_velocidad, lv_color_hex(0x00FF88), LV_PART_MAIN);
    lv_label_set_text(lbl_velocidad, "0 km/h");
    lv_obj_align(lbl_velocidad, LV_ALIGN_BOTTOM_MID, 0, -52);

    // Etiqueta inferior: Distancia a destino y progreso
    lbl_destino = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl_destino, lv_color_hex(0xAAAAAA), LV_PART_MAIN);
    lv_label_set_text(lbl_destino, "Dest: 0 m | 0%");
    lv_obj_align(lbl_destino, LV_ALIGN_BOTTOM_MID, 0, -24);

    // Iniciar en estado desconectado (o conectado si BLE enlazó durante el arranque)
    bool initially_connected = false;
    portENTER_CRITICAL(&state_mux);
    initially_connected = ble_is_connected;
    portEXIT_CRITICAL(&state_mux);

    apply_ui_state(initially_connected ? UI_STATE_CONNECTED_STANDBY : UI_STATE_DISCONNECTED);
}

void update_ui() {
    if (route_line != nullptr) {
        lv_line_set_points(route_line, current_route_points, current_telemetry.numPuntosRuta);
    }

    if (lbl_maniobra != nullptr) {
        lv_label_set_text_fmt(
            lbl_maniobra,
            "Maniobra %d: %d m",
            current_telemetry.tipoManiobra,
            current_telemetry.distanciaManiobra
        );
    }

    if (lbl_velocidad != nullptr) {
        lv_label_set_text_fmt(lbl_velocidad, "%d km/h", current_telemetry.velocidadActual);
    }

    if (lbl_destino != nullptr) {
        lv_label_set_text_fmt(
            lbl_destino,
            "Dest: %d m | %d%%",
            current_telemetry.distanciaDestino,
            current_telemetry.progresoViaje
        );
    }
}

static void clear_navigation_telemetry() {
    current_telemetry = TelemetryData();
    if (route_line != nullptr) {
        lv_line_set_points(route_line, current_route_points, 0);
    }
}

// ============================================================================
// Setup y Loop Principal
// ============================================================================
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("=== Iniciando ZamosaRide Firmware (ESP32-S3) ===");

    init_ble();
    init_display_and_lvgl();
    init_ui();
}

void loop() {
    bool is_connected = false;
    uint32_t disconnected_since = 0;
    bool has_new_telemetry = false;

    portENTER_CRITICAL(&state_mux);
    is_connected = ble_is_connected;
    disconnected_since = ble_disconnected_since_ms;
    if (new_telemetry_available) {
        new_telemetry_available = false;
        has_new_telemetry = true;
        current_telemetry = shared_telemetry;
        for (size_t i = 0; i < current_telemetry.numPuntosRuta; ++i) {
            current_route_points[i] = shared_route_points[i];
        }
    }
    portEXIT_CRITICAL(&state_mux);

    // 1. Transición inmediata al conectar BLE desde estado desconectado
    if (is_connected && current_ui_state == UI_STATE_DISCONNECTED) {
        apply_ui_state(UI_STATE_CONNECTED_STANDBY);
    }

    // 2. Procesamiento de paquetes JSON entrantes (Navegación activa vs Cierre/Sincronización)
    if (has_new_telemetry) {
        bool route_is_active =
            (current_telemetry.distanciaDestino > 0) || (current_telemetry.numPuntosRuta > 0);

        if (route_is_active) {
            if (current_ui_state != UI_STATE_NAVIGATING) {
                apply_ui_state(UI_STATE_NAVIGATING);
            }
            update_ui();
        } else {
            clear_navigation_telemetry();
            if (is_connected && current_ui_state != UI_STATE_CONNECTED_STANDBY) {
                apply_ui_state(UI_STATE_CONNECTED_STANDBY);
            }
        }
    }

    // 3. Si el enlace BLE cae y permanece desconectado por más de 3 segundos -> UI_STATE_DISCONNECTED
    if (!is_connected && current_ui_state != UI_STATE_DISCONNECTED) {
        uint32_t elapsed_disc = (uint32_t)(millis() - disconnected_since);
        if (elapsed_disc >= BLE_DISCONNECT_GRACE_MS) {
            clear_navigation_telemetry();
            apply_ui_state(UI_STATE_DISCONNECTED);
        }
    }

    // 4. Actualizar reloj local en Standby Conectado únicamente cuando cambie el minuto
    if (current_ui_state == UI_STATE_CONNECTED_STANDBY) {
        update_standby_clock();
    }

    lv_timer_handler();
    delay(5);
}
