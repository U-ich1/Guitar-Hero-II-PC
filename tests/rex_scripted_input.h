#pragma once
// Optional in-process controller fixture. Enabled only by GH2_TEST_INPUT.
#include <rex/input/input_system.h>
#include <rex/input/nop/nop_input_driver.h>
#include <chrono>
#include <fstream>
#include <mutex>
#include <rex/logging.h>

using rex::X_STATUS;
using rex::X_RESULT;
using rex::input::X_INPUT_STATE;
using rex::input::X_INPUT_CAPABILITIES;
using rex::input::X_INPUT_VIBRATION;
using rex::input::X_INPUT_KEYSTROKE;

class GH2ScriptedInput final : public rex::input::InputDriver {
    struct Pulse { double seconds, duration; uint16_t buttons; };
    std::vector<Pulse> pulses_;
    std::string path_;
    std::mutex mutex_;
    unsigned reload_second_ = UINT32_MAX;
    rex::input::nop::NopInputDriver idle_{nullptr, 0};
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
    static constexpr auto id_ = static_cast<rex::input::DeviceId>(0x47483201);
    static constexpr auto idle_id_ = static_cast<rex::input::DeviceId>(0x4E4F5000);
public:
    explicit GH2ScriptedInput(const char* path) : InputDriver(nullptr, 0), path_(path) {}
    X_STATUS Setup() override { return X_STATUS_SUCCESS; }
    void EnumerateDevices(std::vector<rex::input::DeviceInfo>& out) override {
        rex::input::DeviceInfo info;
        info.id = id_; info.name = "GH2 controller test"; info.synthetic = true;
        out.push_back(std::move(info));
    }
    X_RESULT GetDeviceState(rex::input::DeviceId, X_INPUT_STATE* state) override {
        idle_.GetDeviceState(idle_id_, state);
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
        std::lock_guard lock(mutex_);
        if (unsigned(elapsed) != reload_second_) {
            reload_second_ = unsigned(elapsed);
            std::ifstream file(path_);
            if (!file) throw std::runtime_error("Controller fixture not found");
            pulses_.clear();
            Pulse pulse{};
            unsigned buttons;
            while (file >> pulse.seconds >> buttons >> pulse.duration) {
                pulse.buttons = uint16_t(buttons);
                pulses_.push_back(pulse);
            }
            if (reload_second_ % 10 == 0) REXLOG_INFO("GH2 scripted controller elapsed: {} seconds", reload_second_);
        }
        for (const auto& pulse : pulses_)
            if (elapsed >= pulse.seconds && elapsed < pulse.seconds + pulse.duration)
                state->gamepad.buttons = uint16_t(state->gamepad.buttons) | pulse.buttons;
        state->packet_number = uint32_t(elapsed * 1000);
        return X_ERROR_SUCCESS;
    }
    X_RESULT GetDeviceCapabilities(rex::input::DeviceId, uint32_t flags, X_INPUT_CAPABILITIES* caps) override {
        return idle_.GetDeviceCapabilities(idle_id_, flags, caps);
    }
    X_RESULT SetDeviceVibration(rex::input::DeviceId, X_INPUT_VIBRATION*) override { return X_ERROR_SUCCESS; }
    X_RESULT GetDeviceKeystroke(rex::input::DeviceId, uint32_t, X_INPUT_KEYSTROKE*) override { return X_ERROR_EMPTY; }
};
