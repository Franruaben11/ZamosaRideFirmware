# Estado del Proyecto - ZamosaRide Firmware (ESP32-S3)

## Fases Completadas
- **Inicialización (PlatformIO):** Entorno configurado para `esp32-s3-devkitc-1` con PSRAM QSPI, 16MB de Flash, `-D LV_COLOR_FORMAT_DEFAULT=LV_COLOR_FORMAT_RGB565` (LVGL 9) y fuentes `LV_FONT_MONTSERRAT_16`, `24` y `48`.
- **Hardware y Display:** Driver QSPI AMOLED integrado mediante `Arduino_GFX` (`Arduino_CO5300` con `col_offset = 6`, encendido de rieles LDO del PMIC `AXP2101` `0x34` a 3.3V por I2C, pulso de reset en `TCA9554` `0x20` + `GPIO 1` + `GPIO 39`, y `draw16bitRGBBitmap` con buffer DMA interno). Doble buffer en PSRAM (`ps_malloc`) con fallback automático a SRAM.
- **Comunicaciones (BLE) y Thread-Safety:** Servidor NimBLE activo (`init_ble()` ejecutado antes de `init_display_and_lvgl()`) con MTU 512 y overrides `bleInUse()`/`btInUse()`. Los callbacks `onConnect`, `onDisconnect` y `onWrite` actualizan únicamente variables compartidas protegidas con `portMUX_TYPE state_mux` y flags `volatile`, sin invocar funciones de LVGL fuera de `loop()`.
- **Máquina de Estados UI y Reloj Local:**
  - `UI_STATE_DISCONNECTED`: muestra ícono Bluetooth (`#888888`), `"ZAMOSARIDE"` (`#FFFFFF`), `"CONECTA TU DISPOSITIVO"` (`#888888`) y `"Esperando conexion..."` (`#555555`) sobre fondo `#000000`. Se activa al encender o tras >3 s sin enlace BLE.
  - `UI_STATE_CONNECTED_STANDBY`: muestra indicador `"CONECTADO"` (`#50C8A0`), hora 24h `"HH:MM"` en `montserrat_48` (`#FFFFFF`, o `"--:--"` si aún no sincronizó) y `"LISTO PARA NAVEGAR"` (`#888888`). El tiempo avanza localmente con `millis()` a partir del campo `"hora"` del JSON BLE y solo redibuja el label cuando cambia el minuto.
  - `UI_STATE_NAVIGATING`: muestra `route_line` (14px, `#00E5FF`) y etiquetas de maniobra, velocidad y destino cuando llegan tramas con ruta activa (`distanciaDestino > 0` o `numPuntosRuta > 0`), regresando a Standby Conectado al recibir el paquete de cierre (`distanciaDestino: 0`, `puntosRuta: []`).

## Notas Técnicas para Flasheo
- Si el ESP32-S3 no entra en modo descarga automáticamente al subir el código: mantener presionado el botón BOOT de la placa, tocar el botón RESET, soltar RESET y por último soltar BOOT.

## Registro de Cambios Recientes
- Se creó el archivo `README.md` documentando el hardware (ESP32-S3, AMOLED Waveshare 1.75"), pinout QSPI/I2C, UUIDs y especificación del payload JSON BLE, comandos de compilación/carga de PlatformIO y estructura del proyecto.
- Se agregaron optimizaciones de ahorro de energía en `src/main.cpp`: brillo AMOLED adaptativo por estado (`255` en navegación, `160` en standby conectado, `60` en desconectado) con auto-apagado (`gfx->displayOff()`) tras 3 min desconectado y encendido inmediato ante actividad BLE; sleep dinámico en `loop()` mediante `xTaskNotifyGive`/`ulTaskNotifyTake` y `lv_timer_handler()`; escalado de frecuencia de CPU (`80 MHz` en standby, `160 MHz` en navegación); y parámetros BLE relajados (`min=40`, `max=80`, `latency=4`, `timeout=400`).
