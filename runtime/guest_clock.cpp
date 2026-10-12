#include "guest_clock.h"
#include <windows.h>
#include <stdexcept>
uint64_t gh2_read_timebase() {
    static const uint64_t frequency = [] {
        LARGE_INTEGER f{};
        if (!QueryPerformanceFrequency(&f) || f.QuadPart <= 0) throw std::runtime_error("Performance clock unavailable");
        return uint64_t(f.QuadPart);
    }();
    LARGE_INTEGER counter{};
    if (!QueryPerformanceCounter(&counter)) throw std::runtime_error("Performance clock read failed");
    return uint64_t(static_cast<unsigned __int128>(uint64_t(counter.QuadPart)) * gh2_timebase_frequency / frequency);
}
