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

#include "Display.h"
#include "NavState.h"
#include "Units.h"

namespace ZamosaRide {

class App {
public:
    explicit App(UnitSettings units = {});

    void setState(const NavState& state);
    const NavState& state() const;

    void tick(uint32_t elapsedMs);

    bool isAnimating() const;
    bool needsProgressAnimationFrame() const;

    void render(Display& display);
    void renderProgressAnimation(Display& display);

private:
    UnitSettings _units;
    NavState _state;
    uint32_t _timeMs = 0;
    float _displayTripProgress = -1.0f;
    float _displayManeuverProgress = -1.0f;
    float _displayDestinationBearingDegrees = -1000.0f;
    int _lastRenderedTripProgress = -2;
    int _lastRenderedManeuverProgress = -2;
    int _lastRenderedDestinationBearingDegrees = -1000;

    void renderNavigation(Display& display);
    void renderDestination(Display& display);
    void renderRideInfo(Display& display);
    void renderNoPhone(Display& display);
    void renderCalibration(Display& display);
};

} // namespace ZamosaRide
