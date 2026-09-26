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

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ZamosaRideGrayAlphaImage {
    uint16_t width;
    uint16_t height;
    const uint8_t* pixels;
} ZamosaRideGrayAlphaImage;

extern const ZamosaRideGrayAlphaImage ZamosaRideDtcLogo;
extern const ZamosaRideGrayAlphaImage ZamosaRideFinishFlag;
extern const ZamosaRideGrayAlphaImage ZamosaRideDirectionContinue;
extern const ZamosaRideGrayAlphaImage ZamosaRideDirectionBendLeft;
extern const ZamosaRideGrayAlphaImage ZamosaRideDirectionExitLeft;
extern const ZamosaRideGrayAlphaImage ZamosaRideDirectionSlightLeft;
extern const ZamosaRideGrayAlphaImage ZamosaRideDirectionTurnLeft;
extern const ZamosaRideGrayAlphaImage ZamosaRideDirectionSharpLeft;
extern const ZamosaRideGrayAlphaImage ZamosaRideDirectionUTurnLeft;
extern const ZamosaRideGrayAlphaImage ZamosaRideDirectionHeading;
extern const ZamosaRideGrayAlphaImage ZamosaRideRoundaboutRoute;
extern const ZamosaRideGrayAlphaImage ZamosaRideRoundaboutNonExit;

#ifdef __cplusplus
}
#endif
