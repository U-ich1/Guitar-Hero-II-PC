#pragma once
#include <array>
#include <cstdint>
#include <algorithm>
#include <atomic>
#include "local_profile.h"

namespace gh2::controls {
enum Source { None, A, B, X, Y, LB, RB, LT, RT, Up, Down, Left, Right, Start, Back, LS, RS, LSUp, LSDown, RSUp, RSDown, SourceCount };
inline constexpr const char* source_names[] = {"Unbound", "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "D-pad up", "D-pad down", "D-pad left", "D-pad right", "Start", "Back", "Left stick click", "Right stick click", "Left stick up / tilt", "Left stick down", "Right stick up / tilt", "Right stick down"};
inline constexpr uint16_t bits[] = {0,0x1000,0x2000,0x4000,0x8000,0x100,0x200,0,0,1,2,4,8,0x10,0x20,0x40,0x80,0,0,0,0};
enum Action { Green, Red, Yellow, Blue, Orange, StrumUp, StrumDown, StarPower, Pause, ActionCount };
inline constexpr const char* action_names[] = {"Green", "Red", "Yellow", "Blue", "Orange", "Strum up / menu up", "Strum down / menu down", "Star Power", "Pause / Start"};
struct State {
    uint16_t buttons{};
    uint8_t lt{}, rt{};
    std::array<int16_t,4> axes{};
};
struct Profile {
    int mode = 0; // Native; remapped native layout; gamepad as strummed guitar.
    std::array<int,ActionCount> sources{};
    int whammy = 0; // Native axes, LX, LY, RX, RY.
};
inline Profile defaults(bool guitar) {
    return {0, guitar ? std::array<int,ActionCount>{A,B,Y,X,LB,Up,Down,Back,Start}
                      : std::array<int,ActionCount>{LT,LB,RB,RT,A,Up,Down,Back,Start}, 0};
}
inline bool is_guitar(uint8_t type) { return type == 6 || type == 7 || type == 11; }
inline bool pressed(const State& state, int source) {
    if (source <= None || source >= SourceCount) return false;
    if (source == LT) return state.lt >= 128;
    if (source == RT) return state.rt >= 128;
    if (source == LSUp) return state.axes[1] >= 16000;
    if (source == LSDown) return state.axes[1] <= -16000;
    if (source == RSUp) return state.axes[3] >= 16000;
    if (source == RSDown) return state.axes[3] <= -16000;
    return (state.buttons & bits[source]) != 0;
}
inline State apply(const State& input, const Profile& profile, bool guitar) {
    if (profile.mode == 0) return input;
    State output{};
    output.axes = input.axes;
    // Retain left/right navigation. B is otherwise unused by the standard
    // gamepad's fret scheme, so it remains its native menu Cancel button.
    output.buttons = input.buttons & (bits[Left] | bits[Right]);
    const bool guitar_layout = guitar || profile.mode == 2;
    // Native guitars may use trigger fields for analogue controls. Preserve
    // those fields when only their fret/strum buttons are being remapped.
    if (guitar) { output.lt=input.lt; output.rt=input.rt; }
    if (!guitar_layout) output.buttons |= input.buttons & bits[B];
    const auto targets = guitar_layout ? defaults(true).sources : defaults(false).sources;
    for (int action = 0; action < ActionCount; ++action) {
        if (!pressed(input, profile.sources[action])) continue;
        const int target = targets[action];
        if (target == LT) output.lt = 255;
        else if (target == RT) output.rt = 255;
        else output.buttons |= bits[target];
    }
    if (profile.whammy > 0 && profile.whammy <= 4) {
        output.axes[0] = input.axes[size_t(profile.whammy - 1)];
    }
    return output;
}
struct Settings {
    std::atomic<int> mode{0}, whammy{0};
    std::array<std::atomic<int>,ActionCount> sources{};
    explicit Settings(bool guitar) {
        auto profile = defaults(guitar);
        for (int i=0; i<ActionCount; ++i) sources[i].store(profile.sources[i]);
    }
    Profile snapshot() const {
        Profile result; result.mode=mode.load(); result.whammy=whammy.load();
        for (int i=0; i<ActionCount; ++i) result.sources[i]=sources[i].load();
        return result;
    }
};
inline Settings gamepad_settings(false), guitar_settings(true);
inline std::array<std::atomic<uint8_t>,4> detected_type{};
inline std::array<std::atomic<uint16_t>,4> live_buttons{};
inline std::array<std::atomic<uint8_t>,4> live_lt{}, live_rt{};
inline std::array<std::array<std::atomic<int16_t>,4>,4> live_axes{};
inline uint16_t read16(const uint8_t* bytes) { return uint16_t(bytes[0]) << 8 | bytes[1]; }
inline void write16(uint8_t* bytes, uint16_t value) { bytes[0]=uint8_t(value>>8); bytes[1]=uint8_t(value); }
inline void remap_guest_state(uint8_t* bytes, unsigned player, uint32_t status) {
    if (!bytes || status || player >= 4) return;
    State state{read16(bytes+4),bytes[6],bytes[7]};
    for (unsigned i=0;i<4;++i) state.axes[i]=int16_t(read16(bytes+8+i*2));
    const auto previous=live_buttons[player].exchange(state.buttons);
    gh2::profile::observe_buttons(player,state.buttons,previous);
    live_lt[player].store(state.lt); live_rt[player].store(state.rt);
    for (unsigned i=0;i<4;++i) live_axes[player][i].store(state.axes[i]);
    const bool guitar=is_guitar(detected_type[player].load());
    const auto output=apply(state,(guitar?guitar_settings:gamepad_settings).snapshot(),guitar);
    write16(bytes+4,output.buttons); bytes[6]=output.lt; bytes[7]=output.rt;
    for (unsigned i=0;i<4;++i) write16(bytes+8+i*2,uint16_t(output.axes[i]));
}
inline void adapt_capabilities(uint8_t* bytes, unsigned player, uint32_t status) {
    if (!bytes || status || player >= 4) return;
    detected_type[player].store(bytes[1]);
    if (!is_guitar(bytes[1]) && gamepad_settings.mode.load() == 2) bytes[1]=7;
}
}
