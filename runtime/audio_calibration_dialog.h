#pragma once
#include "audio_delay.h"
#include <rex/ui/imgui_dialog.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <vector>

class AudioCalibrationDialog final : public rex::ui::ImGuiDialog {
    std::filesystem::path config_;
    bool visible_ = false, test_ = false;
    SDL_AudioStream* stream_ = nullptr;
    bool audio_initialized_ = false;
    gh2::AudioDelay delay_;
    std::vector<float> click_;
    double start_ = 0, flash_ = -10;
    int beat_ = -1;
    std::string error_;
    bool logged_draw_ = false;
    void StopTest() {
        test_ = false;
        if (stream_) SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
        if (audio_initialized_) SDL_QuitSubSystem(SDL_INIT_AUDIO);
        audio_initialized_ = false;
    }
    void StartTest() {
        StopTest();
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) { error_ = SDL_GetError(); return; }
        audio_initialized_ = true;
        SDL_AudioSpec spec{SDL_AUDIO_F32LE, 2, 48000};
        stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (!stream_ || !SDL_ResumeAudioStreamDevice(stream_)) {
            error_ = SDL_GetError(); StopTest(); return;
        }
        error_.clear(); test_ = true; beat_ = -1;
        REXLOG_INFO("GH2 flash/click audio reference started");
        start_ = ImGui::GetTime();
        // Clear the reference delay state, including when a test is restarted.
        delay_.process(nullptr, 0, 2, 0);
        click_.resize(48000 * 2);
    }
protected:
    bool WantsContinuousRepaint() const override { return visible_; }
    void OnDraw(ImGuiIO&) override {
        if (!visible_) return;
        if (!logged_draw_) {
            REXLOG_INFO("GH2 audio calibration panel drawing"); logged_draw_ = true;
            if (std::getenv("GH2_AUDIO_REFERENCE_TEST")) StartTest();
        }
        ImGui::SetNextWindowSize(ImVec2(530, 440), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Audio calibration (F8)", &visible_)) {
            ImGui::TextWrapped("Adjust when sound plays. This is separate from GH2's video calibration.");
            int offset = gh2::audio_offset_ms();
            bool changed = ImGui::InputInt("Audio delay (ms)", &offset, 5, 25);
            if (ImGui::Button("-5 ms")) { offset -= 5; changed = true; }
            ImGui::SameLine();
            if (ImGui::Button("+5 ms")) { offset += 5; changed = true; }
            ImGui::SameLine();
            if (ImGui::Button("Reset to 0")) { offset = 0; changed = true; }
            if (changed) rex::cvar::SetFlagByName("audio_offset_ms", std::to_string(std::clamp(offset, 0, 250)));
            ImGui::TextWrapped("0 adds no delay. Positive values play sound later. If sound is already late, reduce this toward 0.");
            ImGui::TextWrapped("Change this between songs: changing the delay briefly resets the audio buffer.");
            bool lower_buffer = rex::cvar::GetFlagByName("audio_maxqframes") == "4";
            if (ImGui::Checkbox("Lower audio buffering (restart required)", &lower_buffer))
                rex::cvar::SetFlagByName("audio_maxqframes", lower_buffer ? "4" : "8");
            ImGui::TextWrapped("Lower buffering can help when sound is late, but may cause stuttering. Save and restart to apply it.");
            if (ImGui::Button("Save")) {
                std::filesystem::create_directories(config_.parent_path());
                rex::cvar::SaveConfig(config_);
            }
            ImGui::Separator();
            if (ImGui::Button(test_ ? "Stop flash/click test" : "Start flash/click test")) {
                if (test_) StopTest(); else StartTest();
            }
            if (test_) {
                const double now = ImGui::GetTime();
                const int beat = int(now - start_);
                if (beat != beat_) {
                    beat_ = beat; flash_ = now;
                    std::fill(click_.begin(), click_.end(), 0.0f);
                    for (unsigned frame = 0; frame < 384; ++frame) {
                        const float pulse = 0.12f * float(std::sin(2.0 * 3.141592653589793 * 1000.0 * frame / 48000.0))
                            * (1.0f - float(frame) / 384.0f);
                        click_[frame * 2] = click_[frame * 2 + 1] = pulse;
                    }
                    delay_.process(click_.data(), 48000, 2, gh2::audio_offset_ms());
                    // Don't accumulate seconds of clicks if UI rendering stalls.
                    if (SDL_GetAudioStreamQueued(stream_) > int(click_.size() * sizeof(float)))
                        SDL_ClearAudioStream(stream_);
                    if (!SDL_PutAudioStreamData(stream_, click_.data(), int(click_.size() * sizeof(float)))) {
                        error_ = SDL_GetError(); StopTest();
                    }
                    if (beat_ == 0 && test_) REXLOG_INFO("GH2 flash/click reference audio queued with {} ms added delay", gh2::audio_offset_ms());
                }
                ImGui::ColorButton("Flash", now - flash_ < 0.12 ? ImVec4(1,1,1,1) : ImVec4(0.12f,0.12f,0.12f,1),
                                   ImGuiColorEditFlags_NoTooltip, ImVec2(180,80));
            }
            if (!error_.empty()) ImGui::TextWrapped("Audio test: %s", error_.c_str());
            ImGui::TextWrapped("The flash/click is an approximate output reference. Pause the song first; then finish checking timing in Practice with GH2's video calibration.");
        }
        ImGui::End();
        if (!visible_) StopTest();
    }
public:
    AudioCalibrationDialog(rex::ui::ImGuiDrawer* drawer, std::filesystem::path config)
        : ImGuiDialog(drawer), config_(std::move(config)) {}
    ~AudioCalibrationDialog() override { StopTest(); }
    void Toggle() { imgui_drawer()->RequestRepaint(); visible_ = !visible_; if (!visible_) StopTest(); }
};
