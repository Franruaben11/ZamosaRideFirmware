// Code authored by Dean Edis (DeanTheCoder).
// Anyone is free to copy, modify, use, compile, or distribute this software,
// either in source code form or as a compiled binary, for any purpose.
//
// If you modify the code, please retain this copyright header,
// and consider contributing back to the repository or letting us know
// about your modifications. Your contributions are valued!
//
// THE SOFTWARE IS PROVIDED AS IS, WITHOUT WARRANTY OF ANY KIND.

#pragma once

#include "NavState.h"

#include <cstddef>

namespace ZamosaRide {

enum class NavPacketType {
    State,
    Update,
    Heartbeat
};

enum NavFieldMask : uint32_t {
    NavFieldNone = 0,
    NavFieldMode = 1u << 0,
    NavFieldManeuver = 1u << 1,
    NavFieldLink = 1u << 2,
    NavFieldDistanceToManeuver = 1u << 3,
    NavFieldDistanceToDestination = 1u << 4,
    NavFieldManeuverProgress = 1u << 5,
    NavFieldTripProgress = 1u << 6,
    NavFieldDestinationBearing = 1u << 7,
    NavFieldRoundabout = 1u << 8,
    NavFieldCurrentSpeed = 1u << 9,
    NavFieldSpeedLimit = 1u << 10,
    NavFieldSpeedUnit = 1u << 11,
    NavFieldOffRoute = 1u << 12
};

struct NavPacket {
    NavPacketType type = NavPacketType::State;
    NavState state;
    uint32_t fields = NavFieldNone;
};

bool parseNavPacketJson(const char* json, NavPacket& packet);
bool parseNavPacketJson(const char* json, size_t length, NavPacket& packet);
bool parseNavStateJson(const char* json, NavState& state);
bool parseNavStateJson(const char* json, size_t length, NavState& state);

} // namespace ZamosaRide
