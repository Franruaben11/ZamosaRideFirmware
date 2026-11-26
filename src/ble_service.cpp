#include "ble_service.h"
#include <NimBLEDevice.h>
#include <ArduinoJson.h>
#include <cstdio>

// En Arduino-ESP32 v3.3.x, initArduino() libera la memoria del controlador BLE (~36KB en 0x3fcef180)
// al heap general salvo que bleInUse() / btInUse() devuelvan true. Sin esto, NimBLE corrompe el heap al iniciar.
extern "C" bool btInUse(void) { return true; }
extern "C" bool bleInUse(void) { return true; }

static portMUX_TYPE state_mux = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t ble_notify_task = nullptr;

static TelemetryData shared_telemetry;
static lv_point_precise_t shared_route_points[MAX_ROUTE_POINTS];
static volatile bool new_telemetry_available = false;

static volatile bool ble_is_connected = false;
static volatile uint32_t ble_disconnected_since_ms = 0;

static volatile bool time_is_synced = false;
static volatile uint32_t synced_day_seconds = 0;
static volatile uint32_t synced_at_millis = 0;

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
        pServer->updateConnParams(desc->conn_handle, 40, 80, 4, 400);

        if (ble_notify_task != nullptr) {
            xTaskNotifyGive(ble_notify_task);
        }
    }

    void onDisconnect(NimBLEServer *pServer) override {
        uint32_t now_ms = millis();
        portENTER_CRITICAL(&state_mux);
        ble_is_connected = false;
        ble_disconnected_since_ms = now_ms;
        portEXIT_CRITICAL(&state_mux);

        Serial.println("[BLE] Cliente desconectado, reiniciando advertising...");
        NimBLEDevice::startAdvertising();

        if (ble_notify_task != nullptr) {
            xTaskNotifyGive(ble_notify_task);
        }
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
        parsed.limiteVelocidad   = doc["limiteVelocidad"] | 0;
        parsed.distanciaRadar    = doc["distanciaRadar"] | 0;

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

        if (ble_notify_task != nullptr) {
            xTaskNotifyGive(ble_notify_task);
        }

        Serial.printf(
            "[BLE->JSON] Maniobra:%d | DistMan:%dm | DistDest:%dm | Vel:%dkm/h | Prog:%d%% | Radar:%d(%dm) | Puntos:%u | Hora:%s\n",
            parsed.tipoManiobra,
            parsed.distanciaManiobra,
            parsed.distanciaDestino,
            parsed.velocidadActual,
            parsed.progresoViaje,
            parsed.limiteVelocidad,
            parsed.distanciaRadar,
            (unsigned int)parsed.numPuntosRuta,
            doc["hora"] | "--:--"
        );
    }
};

void init_ble(TaskHandle_t notify_task_handle) {
    ble_notify_task = notify_task_handle;

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

bool ble_fetch_state(
    bool *out_is_connected,
    uint32_t *out_disconnected_since_ms,
    TelemetryData *out_telemetry,
    lv_point_precise_t *out_raw_points
) {
    bool has_new = false;
    portENTER_CRITICAL(&state_mux);
    *out_is_connected = ble_is_connected;
    *out_disconnected_since_ms = ble_disconnected_since_ms;
    if (new_telemetry_available) {
        new_telemetry_available = false;
        has_new = true;
        *out_telemetry = shared_telemetry;
        for (size_t i = 0; i < shared_telemetry.numPuntosRuta; ++i) {
            out_raw_points[i] = shared_route_points[i];
        }
    }
    portEXIT_CRITICAL(&state_mux);
    return has_new;
}

bool ble_is_client_connected() {
    bool connected = false;
    portENTER_CRITICAL(&state_mux);
    connected = ble_is_connected;
    portEXIT_CRITICAL(&state_mux);
    return connected;
}

bool ble_get_synced_time(uint32_t *out_base_seconds, uint32_t *out_base_millis) {
    bool synced = false;
    portENTER_CRITICAL(&state_mux);
    synced = time_is_synced;
    *out_base_seconds = synced_day_seconds;
    *out_base_millis = synced_at_millis;
    portEXIT_CRITICAL(&state_mux);
    return synced;
}
