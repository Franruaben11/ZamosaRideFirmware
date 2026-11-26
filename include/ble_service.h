#ifndef ZAMOSA_BLE_SERVICE_H
#define ZAMOSA_BLE_SERVICE_H

#include <Arduino.h>
#include <lvgl.h>
#include "board_config.h"

struct TelemetryData {
    int tipoManiobra = 0;
    int distanciaManiobra = 0;
    int distanciaDestino = 0;
    int velocidadActual = 0;
    int progresoViaje = 0;
    int limiteVelocidad = 0;
    int distanciaRadar = 0;
    size_t numPuntosRuta = 0;
};

// Inicializa el servidor NimBLE y registra el TaskHandle_t principal para notificaciones FreeRTOS
void init_ble(TaskHandle_t notify_task_handle);

// Copia de forma thread-safe el estado BLE y consume cualquier paquete de telemetría nuevo
bool ble_fetch_state(
    bool *out_is_connected,
    uint32_t *out_disconnected_since_ms,
    TelemetryData *out_telemetry,
    lv_point_precise_t *out_raw_points
);

// Consulta thread-safe del estado de conexión BLE actual
bool ble_is_client_connected();

// Consulta thread-safe de la hora base sincronizada por BLE
bool ble_get_synced_time(uint32_t *out_base_seconds, uint32_t *out_base_millis);

#endif // ZAMOSA_BLE_SERVICE_H
