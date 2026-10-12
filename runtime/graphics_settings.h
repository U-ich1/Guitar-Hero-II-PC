#pragma once
#include <rex/cvar.h>
#include <array>
#include <string>
#include <algorithm>
#include "scene_settings.h"
namespace gh2::graphics {
inline int number(const char* name,int fallback=0) {
    const auto text=rex::cvar::GetFlagByName(name);if(text.empty())return fallback;return std::atoi(text.c_str());
}
inline bool read_bool(const char* name,bool fallback=false) {
    const auto text=rex::cvar::GetFlagByName(name);return text.empty()?fallback:(text=="true"||text=="1");
}
inline int configured_scale() {
    return rex::cvar::HasNonDefaultValue("draw_resolution_scale_x")?number("draw_resolution_scale_x",1):number("resolution_scale",1);
}
inline bool set_scale(int scale) {
    if(scale<1||scale>4)return false;
    // Explicit per-axis values avoid old advanced settings overriding the shared preset.
    return rex::cvar::SetFlagByName("resolution_scale",std::to_string(scale))&&
           rex::cvar::SetFlagByName("draw_resolution_scale_x",std::to_string(scale))&&
           rex::cvar::SetFlagByName("draw_resolution_scale_y",std::to_string(scale));
}
inline constexpr std::array<std::array<int,2>,5> window_sizes{{{1280,720},{1600,900},{1920,1080},{2560,1440},{3840,2160}}};
inline bool set_window_size(int index) {
    if(index<0||index>=int(window_sizes.size()))return false;
    return rex::cvar::SetFlagByName("window_width",std::to_string(window_sizes[index][0]))&&
           rex::cvar::SetFlagByName("window_height",std::to_string(window_sizes[index][1]));
}
}
