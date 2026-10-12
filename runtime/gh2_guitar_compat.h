#pragma once
#include <cstdint>

namespace gh2 {
// Disc 415607E7's poll routine at 8227B3F0 accepts only subtype 7 as
// a guitar (lbz at 8227B460; compare at 8227B464). Windows XInput can
// report the same fret/strum layout as guitar subtype 6 or alternate 11.
// Normalize capability identity only; preserve buttons, axes and triggers.
inline bool normalize_guitar_capabilities(uint8_t* caps, uint32_t result) {
    if (result != 0 || !caps || caps[0] != 1) return false;
    if (caps[1] != 6 && caps[1] != 11) return false;
    caps[1] = 7;
    return true;
}
}
