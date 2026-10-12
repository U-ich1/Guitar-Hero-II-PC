#include "../runtime/audio_delay.h"
#include <rex/cvar.h>
#include <vector>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <atomic>

int main(int argc, char** argv) {
    try {
        if (argc > 1) {
            rex::cvar::LoadConfig(argv[1]);
            if (gh2::audio_offset_ms() != 35) throw std::runtime_error("Saved audio offset did not reload");
            std::cout << "PASS: 35 ms saved offset reloads in a fresh process.\n";
            return 0;
        }
        unsigned cases = 0;
        for (unsigned channels : {2U, 6U}) for (int offset : {0, 1, 5, 35, 250}) {
            gh2::AudioDelay delay;
            const size_t frames = 48000;
            std::vector<float> original(frames * channels), output(frames * channels);
            for (size_t i = 0; i < original.size(); ++i) original[i] = float(i + 1) / float(original.size());
            output = original;
            // Deliberately uneven callback sizes, crossing ring-buffer wraps.
            for (size_t start = 0; start < frames;) {
                const size_t count = std::min(size_t(257), frames - start);
                delay.process(output.data() + start * channels, count, channels, offset);
                start += count;
            }
            const size_t shift = size_t(offset) * 48 * channels;
            for (size_t i = 0; i < output.size(); ++i)
                if (output[i] != (i < shift ? 0.0f : original[i - shift]))
                    throw std::runtime_error("Sample timing/channel ordering mismatch");
            ++cases;
        }
        gh2::AudioDelay delay;
        std::vector<float> data(256 * 2, 1.0f);
        delay.process(data.data(), 256, 2, 5);
        std::fill(data.begin(), data.end(), 2.0f);
        delay.process(data.data(), 256, 2, 0);
        for (float value : data) if (value != 2.0f) throw std::runtime_error("Reset leaked previous delayed audio");
        std::fill(data.begin(), data.end(), 0.0f);
        delay.process(data.data(), 256, 2, 5);
        for (float value : data) if (value != 0.0f) throw std::runtime_error("Restart replayed stale audio");
        if (gh2::audio_offset_ms() != 0) throw std::runtime_error("Default offset changed existing timing");
        for (const char* invalid : {"-5", "251", "hello", "35extra"})
            if (rex::cvar::SetFlagByName("audio_offset_ms", invalid)) throw std::runtime_error("Invalid offset accepted");
        std::atomic<bool> stop{false}, invalid_read{false};
        std::thread reader([&] {
            while (!stop.load()) {
                const int value = gh2::audio_offset_ms();
                if (value < 0 || value > 250) invalid_read.store(true);
            }
        });
        for (unsigned i = 0; i < 1000; ++i) rex::cvar::SetFlagByName("audio_offset_ms", std::to_string(i % 251));
        stop.store(true); reader.join();
        if (invalid_read.load()) throw std::runtime_error("Concurrent offset read invalid");
        if (!rex::cvar::SetFlagByName("audio_offset_ms", "35")) throw std::runtime_error("Offset setter failed");
        rex::cvar::SaveConfig("build/audio_offset_test.toml");
        std::cout << "PASS: " << cases << " exact stereo/surround delay cases; zero passthrough, resets, validation, concurrent setting reads.\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
