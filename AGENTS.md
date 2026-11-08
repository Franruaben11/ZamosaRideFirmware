# AGENTS.md — ZamosaRideFirmware

## 1. Contexto y Objetivo
Este directorio contiene **únicamente** el firmware en C++ para el microcontrolador de la moto. La aplicación móvil (Flutter) ya está terminada y se encarga de todo el procesamiento pesado (GPS, ruteo Valhalla, recorte vectorial).
El objetivo actual es inicializar un proyecto de PlatformIO para un **ESP32-S3** con PSRAM QSPI, levantar la interfaz gráfica y recibir los datos por Bluetooth Low Energy (BLE) para renderizar el minimapa.

## 2. Hardware y Stack Técnico
- **Placa:** ESP32-S3 (Configuración en PlatformIO: `board = esp32-s3-devkitc-1`).
- **Pantalla:** Waveshare 1.75" circular AMOLED táctil (466x466 píxeles, interfaz QSPI).
- **Driver de Pantalla:** Controlador **SH8601**. Usar una rama adaptada de `Arduino_GFX` compatible con esta placa.
- **Motor Gráfico:** **LVGL** (Light and Versatile Graphics Library).
- **Bluetooth:** **NimBLE-Arduino** (h2zero), configurando el MTU a 512 bytes para recibir tramas largas.
- **Deserialización:** **ArduinoJson** (allocando un `StaticJsonDocument` de al menos 512 bytes).

## 3. El Contrato de Datos (BLE JSON)
El teléfono envía periódicamente un JSON de texto plano. El firmware actúa como un monitor "tonto", parsea este JSON y actualiza la UI de LVGL. 
*Nota: El campo `puntosRuta` ya viene procesado en píxeles absolutos para un lienzo de 466x466.*

**Ejemplo exacto del payload entrante:**
```json
{"tipoManiobra":2,"distanciaManiobra":140,"distanciaDestino":3200,"velocidadActual":48,"progresoViaje":35,"puntosRuta":[[233,380],[233,245],[340,245]]}
```

## 4. Tareas Inmediatas para el Agente
- **Inicialización:** Crear la estructura base del proyecto PlatformIO (platformio.ini con las librerías necesarias y configuración de memoria PSRAM/Flash para el S3).

- **Setup de Pantalla y LVGL:** Escribir el código de inicialización QSPI para el SH8601 y vincular los display buffers a LVGL para evitar tearing.

- **Servidor BLE:** Levantar un servidor NimBLE con un Service UUID y Characteristic UUID genéricos por ahora. En el callback onWrite, deserializar el JSON.

- **Renderizado Dinámico:** Crear un objeto lv_line (grosor 12-16px, bordes redondeados). Al recibir un nuevo JSON, extraer el array puntosRuta, guardarlo en un lv_point_precise_t current_route_points[10] estático, y pasárselo a la línea con lv_line_set_points.

## 5. Regla de Registro
Al finalizar cualquier modificación en el código o configuración, es obligatorio actualizar `MEMORY.md` detallando MUY BREVE qué se agregó, qué se modificó y por qué.