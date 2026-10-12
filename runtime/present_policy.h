#pragma once
#include <cstdint>
namespace gh2::graphics {
struct PresentPolicy { uint32_t interval,flags; };
inline PresentPolicy present_policy(bool vsync,bool tearing_supported) {
    // DXGI_PRESENT_RESTART=4, DXGI_PRESENT_ALLOW_TEARING=0x200.
    // Tearing is only valid with sync interval zero.
    return vsync?PresentPolicy{1,0}:PresentPolicy{0,4U|(tearing_supported?0x200U:0U)};
}
}
