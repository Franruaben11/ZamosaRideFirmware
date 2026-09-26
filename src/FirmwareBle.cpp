// Code authored by Dean Edis (DeanTheCoder).
// Anyone is free to copy, modify, use, compile, or distribute this software,
// either in source code form or as a compiled binary, for any purpose.
//
// If you modify the code, please retain this copyright header,
// and consider contributing back to the repository or letting us know
// about your modifications. Your contributions are valued!
//
// THE SOFTWARE IS PROVIDED AS IS, WITHOUT WARRANTY OF ANY KIND.

#include "FirmwareBle.h"

#include "ZamosaRide/NavJson.h"

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEService.h>
#include <BLECharacteristic.h>

namespace {

constexpr const char* DeviceName = "ZamosaRide";
constexpr const char* ServiceUuid = "c6372234-79d6-4a5e-8a57-08a3b7a8a7d1";
constexpr const char* StateCharacteristicUuid = "f6c8d747-fc2c-4ef4-906a-7c8cbf552814";
constexpr const char* DeviceStatusCharacteristicUuid = "fa62492e-5c6f-4a8d-9853-1a996b0c7c5d";

FirmwareBle* owner = nullptr;
BLECharacteristic* deviceStatusCharacteristic = nullptr;

class ZamosaRideBleServerCallbacks final : public BLEServerCallbacks {
public:
    /**
     * Handles BLE client connection events.
     *
     * @param server BLE server that accepted the connection.
     */
    void onConnect(BLEServer* server) override {
        (void)server;
        Serial.println("BLE connected");
        if (owner) {
            owner->setLinkState(ZamosaRide::LinkState::Connected);
        }
    }

    /**
     * Handles BLE client disconnection events.
     *
     * @param server BLE server that lost the connection.
     */
    void onDisconnect(BLEServer* server) override {
        if (owner) {
            owner->setLinkState(ZamosaRide::LinkState::Disconnected);
        }

        server->startAdvertising();
        Serial.println("BLE disconnected; advertising restarted");
    }
};

class ZamosaRideBleCharacteristicCallbacks final : public BLECharacteristicCallbacks {
public:
    /**
     * Handles writes to the navigation-state characteristic.
     *
     * @param characteristic Characteristic containing the written JSON packet.
     */
    void onWrite(BLECharacteristic* characteristic) override {
#if !defined(CONFIG_BLUEDROID_ENABLED)
        const String value = characteristic->getValue();
        if (owner && value.length() > 0) {
            owner->handleWrite(value.c_str(), (size_t)value.length());
        }
#else
        (void)characteristic;
#endif
    }

#if defined(CONFIG_BLUEDROID_ENABLED)
    /**
     * Handles Bluedroid writes using the raw write payload.
     *
     * @param characteristic Characteristic receiving the write.
     * @param param Bluedroid write event parameters.
     */
    void onWrite(BLECharacteristic* characteristic, esp_ble_gatts_cb_param_t* param) override {
        (void)characteristic;
        if (owner && param && param->write.len > 0) {
            owner->handleWrite((const char*)param->write.value, (size_t)param->write.len);
        }
    }
#endif
};

ZamosaRideBleServerCallbacks serverCallbacks;
ZamosaRideBleCharacteristicCallbacks characteristicCallbacks;

} // namespace

void FirmwareBle::begin(PacketCallback callback) {
    _callback = callback;
    owner = this;

    BLEDevice::init(DeviceName);
    BLEServer* server = BLEDevice::createServer();
    server->setCallbacks(&serverCallbacks);

    BLEService* service = server->createService(ServiceUuid);
    BLECharacteristic* stateCharacteristic = service->createCharacteristic(
        StateCharacteristicUuid,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    stateCharacteristic->setCallbacks(&characteristicCallbacks);
    stateCharacteristic->setValue("{\"v\":1,\"device\":\"ZamosaRide\"}");

    deviceStatusCharacteristic = service->createCharacteristic(
        DeviceStatusCharacteristicUuid,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    deviceStatusCharacteristic->setValue("{\"v\":1,\"batteryPercent\":null}");

    service->start();
    server->getAdvertising()->addServiceUUID(ServiceUuid);
    server->startAdvertising();
    setLinkState(ZamosaRide::LinkState::Pairing);

    Serial.println("ZamosaRide BLE advertising");
}

void FirmwareBle::publishDeviceStatus(const char* json) {
    if (!deviceStatusCharacteristic || !json) {
        return;
    }

    deviceStatusCharacteristic->setValue(json);
    if (_linkState == ZamosaRide::LinkState::Connected) {
        deviceStatusCharacteristic->notify();
    }

    Serial.printf("BLE device status published: %.80s\n", json);
}

ZamosaRide::LinkState FirmwareBle::linkState() const {
    return _linkState;
}

void FirmwareBle::handleWrite(const char* data, size_t length) {
    Serial.printf("BLE write chunk: %u bytes\n", (unsigned)length);
    for (size_t i = 0; i < length; ++i) {
        const char ch = data[i];

        if (!_packetStarted && ch != '{') {
            continue;
        }

        if (_packetLength + 1 >= MaxPacketBytes) {
            Serial.println("BLE JSON packet too large");
            _packetLength = 0;
            _jsonDepth = 0;
            _packetStarted = false;
            return;
        }

        _packetStarted = true;
        _packet[_packetLength++] = ch;

        if (ch == '{') {
            ++_jsonDepth;
        } else if (ch == '}') {
            --_jsonDepth;
            if (_jsonDepth <= 0) {
                parseBufferedPacket();
                _packetLength = 0;
                _jsonDepth = 0;
                _packetStarted = false;
            }
        }
    }

}

void FirmwareBle::parseBufferedPacket() {
    _packet[_packetLength] = '\0';
    Serial.printf("BLE JSON packet: %u bytes, prefix: %.24s\n", (unsigned)_packetLength, _packet);

    ZamosaRide::NavPacket packet;
    if (!ZamosaRide::parseNavPacketJson(_packet, _packetLength, packet)) {
        Serial.println("BLE JSON parse failed");
        return;
    }

    packet.state.linkState = _linkState;
    packet.state.connected = _linkState == ZamosaRide::LinkState::Connected;
    packet.fields |= ZamosaRide::NavFieldLink;

    if (_callback) {
        _callback(packet);
    }
}

void FirmwareBle::setLinkState(ZamosaRide::LinkState state) {
    _linkState = state;
}
