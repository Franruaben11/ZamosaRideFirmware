// Code authored by Dean Edis (DeanTheCoder).
// Anyone is free to copy, modify, use, compile, or distribute this software,
// either in source code form or as a compiled binary, for any purpose.
//
// If you modify the code, please retain this copyright header,
// and consider contributing back to the repository or letting us know
// about your modifications. Your contributions are valued!
//
// THE SOFTWARE IS PROVIDED AS IS, WITHOUT WARRANTY OF ANY KIND.

#include "FirmwareDisplay.h"
#include "FirmwareBle.h"
#include "I2C_Driver.h"
#include "TCA9554PWR.h"

#include "ZamosaRide/App.h"

#include <Arduino.h>
#include <BLEDevice.h>
#include <esp_sleep.h>

namespace {

constexpr uint32_t SplashFadeInMs = 900;
constexpr uint32_t SplashHoldMs = 2000;
constexpr uint32_t SplashFadeOutMs = 900;
constexpr uint32_t SplashTotalMs = SplashFadeInMs + SplashHoldMs + SplashFadeOutMs;
constexpr uint32_t AnimationFrameMs = 50;
constexpr uint32_t ActiveLoopDelayMs = 16;
constexpr uint32_t ActiveCpuMhz = 240;
constexpr uint32_t PowerButtonLongPressMs = 3000;
constexpr uint32_t DebounceMs = 50;
constexpr uint32_t DeviceStatusPublishMs = 5 * 60 * 1000;
constexpr uint8_t BatteryAdcPin = 8;
constexpr float BatteryAdcDivider = 3.0f;

enum class PowerState { ON, SUSPENDED, OFF };

PowerState powerState = PowerState::ON;

bool liveBleMode = false;
volatile bool pendingBleState = false;
uint32_t lastPacketMs = 0;
uint32_t lastWakefulActivityMs = 0;
uint32_t bootMs = 0;
uint32_t lastDeviceStatusMs = 0;
bool haveBleState = false;
bool deviceStatusPublishedForConnection = false;
ZamosaRide::NavState lastBleState;
ZamosaRide::NavPacket nextBlePacket;

struct ButtonState {
    bool lastPowerPressed = false;
    bool lastBootPressed = false;
    uint32_t powerPressStartMs = 0;
    bool powerLongPressHandled = false;
} btnState;

uint8_t splashOpacity(uint32_t elapsedMs) {
    if (elapsedMs < SplashFadeInMs) {
        return (uint8_t)((elapsedMs * 255) / SplashFadeInMs);
    }
    if (elapsedMs < SplashFadeInMs + SplashHoldMs) {
        return 255;
    }
    if (elapsedMs < SplashTotalMs) {
        return (uint8_t)(255 - (((elapsedMs - SplashFadeInMs - SplashHoldMs) * 255) / SplashFadeOutMs));
    }
    return 0;
}

ZamosaRide::NavState navigationState(ZamosaRide::Maneuver maneuver, int32_t distanceMeters) {
    ZamosaRide::NavState state;
    state.mode = ZamosaRide::DisplayMode::Navigation;
    state.maneuver = maneuver;
    state.distanceToManeuverMeters = distanceMeters;
    state.distanceToDestinationMeters = 18400;
    state.maneuverProgressRemaining = distanceMeters > 300 ? 85 : 22;
    state.tripProgressComplete = 32;
    state.destinationBearingDegrees = 35;
    return state;
}

ZamosaRide::NavState roundaboutState() {
    ZamosaRide::NavState state = navigationState(ZamosaRide::Maneuver::Roundabout, 260);
    state.roundaboutExitCount = 5;
    state.roundaboutExit = 3;
    state.maneuverProgressRemaining = 58;
    state.tripProgressComplete = 44;
    return state;
}

ZamosaRide::NavState speedingState() {
    ZamosaRide::NavState state = navigationState(ZamosaRide::Maneuver::Continue, 420);
    state.currentSpeed = 55;
    state.speedLimit = 50;
    state.speedUnit = ZamosaRide::SpeedUnit::Mph;
    return state;
}

ZamosaRide::NavState destinationState() {
    ZamosaRide::NavState state;
    state.mode = ZamosaRide::DisplayMode::Destination;
    state.distanceToDestinationMeters = 18400;
    state.destinationBearingDegrees = 35;
    state.tripProgressComplete = 32;
    return state;
}

ZamosaRide::NavState demoState(int screen) {
    switch (screen) {
        case 1: return navigationState(ZamosaRide::Maneuver::TurnLeft, 180);
        case 2: return roundaboutState();
        case 3: return speedingState();
        case 4: return destinationState();
        case 5: return navigationState(ZamosaRide::Maneuver::UTurn, 90);
        case 6: return navigationState(ZamosaRide::Maneuver::BendLeft, 120);
        case 0:
        default: return navigationState(ZamosaRide::Maneuver::Continue, 420);
    }
}

void renderDemoScreen(int screen) {
    if (liveBleMode) return;
    Serial.printf("Demo screen rendered: %d\n", screen);
    static ZamosaRide::App app;
    app.setState(demoState(screen));
    static FirmwareDisplay display;
    app.render(display);
}

void applyUpdate(ZamosaRide::NavState& state, const ZamosaRide::NavPacket& packet) {
    if (packet.fields & ZamosaRide::NavFieldMode) state.mode = packet.state.mode;
    if (packet.fields & ZamosaRide::NavFieldManeuver) state.maneuver = packet.state.maneuver;
    if (packet.fields & ZamosaRide::NavFieldLink) {
        state.connected = packet.state.connected;
        state.linkState = packet.state.linkState;
    }
    if (packet.fields & ZamosaRide::NavFieldDistanceToManeuver) state.distanceToManeuverMeters = packet.state.distanceToManeuverMeters;
    if (packet.fields & ZamosaRide::NavFieldDistanceToDestination) state.distanceToDestinationMeters = packet.state.distanceToDestinationMeters;
    if (packet.fields & ZamosaRide::NavFieldManeuverProgress) state.maneuverProgressRemaining = packet.state.maneuverProgressRemaining;
    if (packet.fields & ZamosaRide::NavFieldTripProgress) state.tripProgressComplete = packet.state.tripProgressComplete;
    if (packet.fields & ZamosaRide::NavFieldDestinationBearing) state.destinationBearingDegrees = packet.state.destinationBearingDegrees;
    if (packet.fields & ZamosaRide::NavFieldRoundabout) {
        state.roundaboutExitCount = packet.state.roundaboutExitCount;
        state.roundaboutExit = packet.state.roundaboutExit;
        state.roundaboutExitAngleCount = packet.state.roundaboutExitAngleCount;
        for (int i = 0; i < ZamosaRide::MaxRoundaboutExits; ++i) {
            state.roundaboutExitAngles[i] = packet.state.roundaboutExitAngles[i];
        }
    }
    if (packet.fields & ZamosaRide::NavFieldCurrentSpeed) state.currentSpeed = packet.state.currentSpeed;
    if (packet.fields & ZamosaRide::NavFieldSpeedLimit) state.speedLimit = packet.state.speedLimit;
    if (packet.fields & ZamosaRide::NavFieldSpeedUnit) state.speedUnit = packet.state.speedUnit;
    if (packet.fields & ZamosaRide::NavFieldOffRoute) state.offRoute = packet.state.offRoute;
}

void applyBlePacket(const ZamosaRide::NavPacket& packet) {
    liveBleMode = true;
    lastPacketMs = millis();
    if (packet.type != ZamosaRide::NavPacketType::Heartbeat) {
        lastWakefulActivityMs = lastPacketMs;
    }
    nextBlePacket = packet;
    pendingBleState = true;
    Serial.printf("BLE packet queued: type=%d fields=%lu\n", (int)packet.type, (unsigned long)packet.fields);
}

uint32_t readBatteryMillivolts() {
    constexpr int SampleCount = 8;
    uint32_t totalMv = 0;
    for (int i = 0; i < SampleCount; ++i) {
        totalMv += (uint32_t)analogReadMilliVolts(BatteryAdcPin);
        delay(2);
    }
    const float adcMv = (float)totalMv / (float)SampleCount;
    return (uint32_t)(adcMv * BatteryAdcDivider + 0.5f);
}

uint8_t batteryPercentFromMillivolts(uint32_t batteryMv) {
    struct Point { uint16_t mv; uint8_t percent; };
    static constexpr Point curve[] = {
        {3300, 0}, {3500, 5}, {3600, 12}, {3700, 25},
        {3800, 42}, {3900, 60}, {4000, 78}, {4100, 92}, {4200, 100}
    };
    if (batteryMv <= curve[0].mv) return curve[0].percent;
    for (size_t i = 1; i < sizeof(curve)/sizeof(curve[0]); ++i) {
        if (batteryMv <= curve[i].mv) {
            const Point lower = curve[i-1];
            const Point upper = curve[i];
            const uint32_t spanMv = upper.mv - lower.mv;
            const uint32_t spanPercent = upper.percent - lower.percent;
            return (uint8_t)(lower.percent + ((batteryMv - lower.mv) * spanPercent) / spanMv);
        }
    }
    return 100;
}

void publishDeviceStatus(uint32_t now, const char* reason) {
    const uint32_t batteryMv = readBatteryMillivolts();
    const uint8_t batteryPercent = batteryPercentFromMillivolts(batteryMv);
    char json[128];
    snprintf(json, sizeof(json), "{\"v\":1,\"batteryMv\":%lu,\"batteryPercent\":%u}", (unsigned long)batteryMv, (unsigned)batteryPercent);
    
    static FirmwareBle ble;
    ble.publishDeviceStatus(json);
    lastDeviceStatusMs = now;
    deviceStatusPublishedForConnection = true;
    Serial.printf("Device status sent: %s battery=%lu mV %u%%\n", reason, (unsigned long)batteryMv, (unsigned)batteryPercent);
}

void enterDeepSleepPMIC() {
    Serial.println("Initiating PMIC shutdown...");
    Serial.flush();
    
    // AXP2101 shutdown sequence
    // Register 0x10 (COMMON_CONFIG), bit 0 = shutdown all rails except VRTC
    uint8_t shutdownCmd = 0x01;
    I2C_Write(0x34, 0x10, &shutdownCmd, 1);
    
    delay(100);
    
    // Fallback: if PMIC doesn't cut power, use ESP deep sleep
    esp_deep_sleep_start();
}

bool readPowerButton() {
    // PWR button state via TCA9554 EXIO4 (SYS_OUT)
    // HIGH = pressed, LOW = released
    return Read_EXIO(4) == High;
}

bool readBootButton() {
    // BOOT button on GPIO0 (active LOW with pullup)
    return digitalRead(0) == LOW;
}

void handlePowerButton(bool pressed, uint32_t now) {
    if (pressed && !btnState.lastPowerPressed) {
        // Rising edge - press started
        btnState.powerPressStartMs = now;
        btnState.powerLongPressHandled = false;
    } else if (!pressed && btnState.lastPowerPressed) {
        // Falling edge - press ended
        uint32_t duration = now - btnState.powerPressStartMs;
        if (duration >= PowerButtonLongPressMs && !btnState.powerLongPressHandled) {
            // Long press >= 3s -> POWER OFF
            Serial.println("Long press detected: Powering OFF");
            powerState = PowerState::OFF;
        } else if (duration < PowerButtonLongPressMs) {
            // Short press -> TOGGLE SUSPEND
            if (powerState == PowerState::ON) {
                Serial.println("Short press: Suspending (screen off, BLE alive)");
                powerState = PowerState::SUSPENDED;
            } else if (powerState == PowerState::SUSPENDED) {
                Serial.println("Short press: Resuming (screen on)");
                powerState = PowerState::ON;
            }
        }
        btnState.powerPressStartMs = 0;
    } else if (pressed && (now - btnState.powerPressStartMs >= PowerButtonLongPressMs) && !btnState.powerLongPressHandled) {
        // Long press detected while holding
        btnState.powerLongPressHandled = true;
        Serial.println("Long press held: Powering OFF");
        powerState = PowerState::OFF;
    }
    btnState.lastPowerPressed = pressed;
}

void handleBootButton(bool pressed, uint32_t now) {
    if (pressed && !btnState.lastBootPressed) {
        // Rising edge on BOOT (active LOW, so this is button release)
        // We'll trigger on release for UI interaction
    } else if (!pressed && btnState.lastBootPressed) {
        // Falling edge - button pressed
        if (powerState == PowerState::ON) {
            Serial.println("BOOT button: UI interaction");
            // Could cycle modes, etc. For now just log
        }
    }
    btnState.lastBootPressed = pressed;
}

} // namespace

FirmwareDisplay display;
FirmwareBle ble;
ZamosaRide::App app;

void setup() {
    Serial.begin(115200);
    bootMs = millis();
    Serial.println("ZamosaRide firmware startup");

    // Initialize I2C (shared bus for AXP2101, TCA9554, etc.)
    I2C_Init();
    
    // Initialize TCA9554 for AXP_IRQ monitoring and PWR button reading
    TCA9554PWR_Init(0x00);

    // Initialize display
    if (!display.begin()) {
        Serial.println("Display init failed");
        return;
    }

    // Show splash screen
    uint32_t splashStart = millis();
    while (millis() - splashStart < SplashTotalMs) {
        display.splash(splashOpacity(millis() - splashStart));
        delay(16);
    }

    // Configure BOOT button (GPIO0) as input with pullup
    pinMode(0, INPUT_PULLUP);

    // Initialize BLE
    ble.begin(applyBlePacket);
    lastWakefulActivityMs = millis();

    powerState = PowerState::ON;
    display.setAwake(true);
    
    Serial.println("System ready - ON state");
}

void loop() {
    uint32_t now = millis();
    static uint32_t lastTick = now;
    static uint32_t lastAnimationFrameMs = 0;

    // Read buttons (debounced)
    bool powerPressed = readPowerButton();
    bool bootPressed = readBootButton();

    // Handle button logic
    handlePowerButton(powerPressed, now);
    handleBootButton(bootPressed, now);

    // State machine
    switch (powerState) {
        case PowerState::ON: {
            // Normal operation
            if (ble.linkState() == ZamosaRide::LinkState::Connected) {
                if (!deviceStatusPublishedForConnection) {
                    publishDeviceStatus(now, "connect");
                } else if (now - lastDeviceStatusMs >= DeviceStatusPublishMs) {
                    publishDeviceStatus(now, "periodic");
                }
            } else {
                deviceStatusPublishedForConnection = false;
            }

            app.tick(now - lastTick);
            lastTick = now;

            if (pendingBleState) {
                pendingBleState = false;
                if (nextBlePacket.type == ZamosaRide::NavPacketType::Heartbeat) {
                    if (haveBleState) {
                        app.setState(lastBleState);
                        app.render(display);
                        Serial.println("BLE heartbeat restored last route state");
                    }
                    Serial.println("BLE heartbeat received");
                } else if (nextBlePacket.type == ZamosaRide::NavPacketType::State) {
                    haveBleState = true;
                    lastBleState = nextBlePacket.state;
                    app.setState(lastBleState);
                    app.render(display);
                    Serial.printf("BLE state rendered: mode=%d maneuver=%d distance=%ld\n", 
                        (int)nextBlePacket.state.mode, (int)nextBlePacket.state.maneuver, (long)nextBlePacket.state.distanceToManeuverMeters);
                } else if (haveBleState) {
                    ZamosaRide::NavState state = lastBleState;
                    applyUpdate(state, nextBlePacket);
                    lastBleState = state;
                    app.setState(lastBleState);
                    app.render(display);
                    Serial.printf("BLE update rendered: fields=%lu distance=%ld\n", 
                        (unsigned long)nextBlePacket.fields, (long)state.distanceToManeuverMeters);
                } else {
                    Serial.println("BLE update ignored before full state");
                }
            }

            if (!pendingBleState && app.isAnimating() && app.needsProgressAnimationFrame() && now - lastAnimationFrameMs >= AnimationFrameMs) {
                app.renderProgressAnimation(display);
                lastAnimationFrameMs = now;
            }

            delay(ActiveLoopDelayMs);
            break;
        }

        case PowerState::SUSPENDED: {
            // Screen off, BLE alive, minimal CPU
            display.setAwake(false);
            
            // Keep BLE connection alive with minimal processing
            if (ble.linkState() == ZamosaRide::LinkState::Connected) {
                // Process any pending BLE packets but don't render
                if (pendingBleState) {
                    pendingBleState = false;
                    if (nextBlePacket.type == ZamosaRide::NavPacketType::Heartbeat) {
                        // Just acknowledge
                    } else if (nextBlePacket.type == ZamosaRide::NavPacketType::State) {
                        haveBleState = true;
                        lastBleState = nextBlePacket.state;
                    } else if (haveBleState) {
                        ZamosaRide::NavState state = lastBleState;
                        applyUpdate(state, nextBlePacket);
                        lastBleState = state;
                    }
                }
            }
            
            delay(100); // Reduced CPU frequency in suspend
            break;
        }

        case PowerState::OFF: {
            // Deep sleep via PMIC
            ble.stopHeartbeatTimer(); // Stop BLE timers
            display.setAwake(false);
            enterDeepSleepPMIC();
            break;
        }
    }
}
