#pragma once
#include <algorithm>
#include <array>
#include <cstddef>

namespace gh2 {
// Per-output-stream delay, after mixing. No allocations or guest clock changes.
class AudioDelay {
    std::array<float, 48000 * 250 / 1000 * 6> samples_{};
    size_t cursor_ = 0, length_ = 0;
    unsigned channels_ = 0;
    int milliseconds_ = -1;
public:
    void process(float* data, size_t frames, unsigned channels, int milliseconds) {
        milliseconds = std::clamp(milliseconds, 0, 250);
        if (channels == 0 || channels > 6) return;
        if (channels_ != channels || milliseconds_ != milliseconds) {
            channels_ = channels;
            milliseconds_ = milliseconds;
            length_ = size_t(milliseconds) * 48 * channels;
            cursor_ = 0;
            std::fill(samples_.begin(), samples_.end(), 0.0f);
        }
        if (!length_) return;
        for (size_t i = 0; i < frames * channels; ++i) {
            std::swap(data[i], samples_[cursor_]);
            if (++cursor_ == length_) cursor_ = 0;
        }
    }
};
int audio_offset_ms();
}
