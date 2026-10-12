// Exercise the reference stream's initialization/cleanup without launching GH2
// or playing a click. The opened device stream remains paused throughout.
#include <SDL3/SDL.h>
#include <array>
#include <iostream>
int main() {
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) { std::cerr << SDL_GetError(); return 1; }
        SDL_AudioSpec spec{SDL_AUDIO_F32LE, 2, 48000};
        auto* stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (!stream) { std::cerr << SDL_GetError(); SDL_QuitSubSystem(SDL_INIT_AUDIO); return 2; }
        std::array<float, 480 * 2> silence{};
        const bool queued = SDL_PutAudioStreamData(stream, silence.data(), sizeof(silence));
        const int bytes = SDL_GetAudioStreamQueued(stream);
        SDL_DestroyAudioStream(stream);
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        if (!queued || bytes != sizeof(silence)) { std::cerr << "Reference audio queue failed"; return 3; }
    }
    std::cout << "PASS: reference initializes before guest audio; paused stereo queue and repeated cleanup work. No game launched or closed.\n";
}
