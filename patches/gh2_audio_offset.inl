// Compiled into rexruntime; shared atomic setting for the output callback and UI.
#include <atomic>
#include <charconv>
#include <rex/cvar.h>
namespace gh2 {
static std::atomic<int> audio_offset{0};
int audio_offset_ms() { return audio_offset.load(std::memory_order_relaxed); }
}
static auto gh2_audio_offset_registration = rex::cvar::FlagRegistrar({
    "audio_offset_ms", rex::cvar::FlagType::Int32, "Audio/Calibration",
    "Delay audio output in milliseconds (0 preserves existing timing; positive values play audio later).",
    [](std::string_view text) {
        int value{};
        auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc{} || end != text.data() + text.size() || value < 0 || value > 250) return false;
        gh2::audio_offset.store(value, std::memory_order_relaxed);
        return true;
    },
    [] { return std::to_string(gh2::audio_offset_ms()); }, {},
    rex::cvar::Lifecycle::kHotReload, {}, "0"
}).range(0, 250);
