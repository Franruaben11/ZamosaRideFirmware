// Code authored by Dean Edis (DeanTheCoder).
// Anyone is free to copy, modify, use, compile, or distribute this software,
// either in source code form or as a compiled binary, for any purpose.
//
// If you modify the code, please retain this copyright header,
// and consider contributing back to the repository or letting us know
// about your modifications. Your contributions are valued!
//
// THE SOFTWARE IS PROVIDED AS IS, WITHOUT WARRANTY OF ANY KIND.

#include "ZamosaRide/Units.h"

#include <cstdint>

namespace ZamosaRide {

FormattedDistance formatDistanceMeters(int32_t meters, UnitSettings settings) {
    FormattedDistance result = {0, 0, "m"};

    switch (settings.distance) {
        case DistanceUnitPreference::MilesMeters: {
            if (meters >= 1609) {
                const int32_t miles = meters / 1609;
                const int32_t remainder = meters % 1609;
                if (remainder >= 805) {
                    result.value = miles * 10 + 5;
                    result.decimalPlaces = 1;
                } else {
                    result.value = miles;
                    result.decimalPlaces = 0;
                }
                result.unit = "mi";
            } else {
                result.value = meters;
                result.decimalPlaces = 0;
                result.unit = "m";
            }
            break;
        }
        case DistanceUnitPreference::MilesFeet: {
            if (meters >= 1609) {
                const int32_t miles = meters / 1609;
                const int32_t remainder = meters % 1609;
                if (remainder >= 805) {
                    result.value = miles * 10 + 5;
                    result.decimalPlaces = 1;
                } else {
                    result.value = miles;
                    result.decimalPlaces = 0;
                }
                result.unit = "mi";
            } else {
                const int32_t feet = meters * 3281 / 1000;
                result.value = feet;
                result.decimalPlaces = 0;
                result.unit = "ft";
            }
            break;
        }
        case DistanceUnitPreference::KilometersMeters: {
            if (meters >= 1000) {
                const int32_t km = meters / 1000;
                const int32_t remainder = meters % 1000;
                if (remainder >= 500) {
                    result.value = km * 10 + 5;
                    result.decimalPlaces = 1;
                } else {
                    result.value = km;
                    result.decimalPlaces = 0;
                }
                result.unit = "km";
            } else {
                result.value = meters;
                result.decimalPlaces = 0;
                result.unit = "m";
            }
            break;
        }
    }

    return result;
}

} // namespace ZamosaRide
