# ZamosaRide Firmware (ESP32-S3)

Firmware en C++ para el microcontrolador de la moto desarrollado sobre PlatformIO y Arduino Framework. Este dispositivo actúa como display/monitor para la aplicación móvil ZamosaRide (Flutter), recibiendo telemetría y minimapas vectoriales precalculados vía Bluetooth Low Energy (BLE) y renderizándolos en tiempo real con LVGL.

---

## 🛠️ Hardware y Especificaciones

- **Microcontrolador:** ESP32-S3 (`esp32-s3-devkitc-1`)
- **Memoria:** Flash QIO / PSRAM QSPI (`qio_qspi`)
- **Pantalla:** Waveshare 1.75" circular AMOLED táctil
  - **Resolución:** 466 x 466 píxeles
  - **Interfaz:** QSPI (Quad SPI)
  - **Driver:** Controlador CO5300 / SH8601
- **Motor Gráfico:** LVGL v9.x (Light and Versatile Graphics Library)
- **Conectividad:** Bluetooth Low Energy (NimBLE-Arduino) con MTU a 512 bytes

---

## 📌 Asignación de Pines (Pinout)

### Bus QSPI (Pantalla AMOLED)
| Señal | Pin ESP32-S3 |
|---|---|
| `LCD_CS` | GPIO 12 |
| `LCD_SCLK` | GPIO 38 |
| `LCD_SDIO0` | GPIO 4 |
| `LCD_SDIO1` | GPIO 5 |
| `LCD_SDIO2` | GPIO 6 |
| `LCD_SDIO3` | GPIO 7 |
| `LCD_RST` | Controlado por Expansor TCA9554 / GPIO 1 / GPIO 39 |

### Bus I2C (Alimentación y Periféricos)
| Señal | Pin ESP32-S3 |
|---|---|
| `IIC_SDA` | GPIO 15 |
| `IIC_SCL` | GPIO 14 |
| **Dispositivos I2C** | TCA9554 (`0x20`), AXP2101 (`0x34`) |

---

## 📡 Protocolo de Comunicación BLE

El ESP32-S3 anuncia un servicio BLE con nombre `ZamosaRide-S3` y escucha tramas JSON en una característica dedicada.

- **Service UUID:** `4fafc201-1fb5-459e-8fcc-c5c9c331914b`
- **Characteristic UUID:** `beb5483e-36e1-4688-b7f5-ea07361b26a8`
- **MTU Requerido:** 512 bytes

### Estructura del Payload JSON

La aplicación móvil envía periódicamente un JSON plano con los datos de navegación y los puntos vectoriales ya proyectados al lienzo de 466x466:

```json
{
  "tipoManiobra": 2,
  "distanciaManiobra": 140,
  "distanciaDestino": 3200,
  "velocidadActual": 48,
  "progresoViaje": 35,
  "puntosRuta": [
    [233, 380],
    [233, 245],
    [340, 245]
  ]
}
```

### Campos:
- `tipoManiobra`: Código de la siguiente maniobra (giro, rotonda, etc.).
- `distanciaManiobra`: Distancia en metros hasta la maniobra.
- `distanciaDestino`: Distancia restante al destino final en metros.
- `velocidadActual`: Velocidad reportada por GPS en km/h.
- `progresoViaje`: Porcentaje de avance de la ruta (0-100%).
- `puntosRuta`: Array de coordenadas `[X, Y]` en píxeles absolutos (0 a 465) que definen el trazo de la ruta en pantalla.

---

## 🚀 Compilación y Carga

### Requisitos Previos
- [PlatformIO Core](https://platformio.org/) o extensión de PlatformIO en Visual Studio Code.
- Cable USB conectado al puerto USB nativo del ESP32-S3.

### Comandos

1. **Compilar el proyecto:**
   ```bash
   pio run
   ```

2. **Flashear el firmware:**
   ```bash
   pio run --target upload
   ```

3. **Abrir monitor serie:**
   ```bash
   pio device monitor
   ```

---

## 📂 Estructura del Repositorio

- `platformio.ini`: Configuración del entorno de compilación, flags de memoria y dependencias de librerías.
- `src/main.cpp`: Código fuente principal (inicialización de buses, driver AMOLED, buffers LVGL, servidor BLE, callbacks JSON y dibujado dinámico de líneas).
- `AGENTS.md`: Contrato y directivas de desarrollo para agentes y desarrolladores.
- `MEMORY.md`: Bitácora de cambios y estados técnicos del proyecto.
