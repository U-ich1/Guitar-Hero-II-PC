#pragma once
#include <algorithm>
#include <bit>
#include <limits>
#include <type_traits>

// XenonRecomp stores vector bytes in reverse guest order. Lane-wise operations
// preserve that representation; a packed result contains B below A on the host.
namespace gh2 {
template<class T> inline T clamp_signed(int64_t value, uint32_t& vscr) {
    const auto lo = int64_t(std::numeric_limits<T>::min());
    const auto hi = int64_t(std::numeric_limits<T>::max());
    if (value < lo || value > hi) vscr |= 1;
    return T(std::clamp(value, lo, hi));
}
template<class T> inline T lane(const PPCVRegister& v, size_t i) {
    T value;
    memcpy(&value, v.u8 + i * sizeof(T), sizeof(T));
    return value;
}
template<class T> inline void put(PPCVRegister& v, size_t i, T value) {
    memcpy(v.u8 + i * sizeof(T), &value, sizeof(T));
}
template<class T, bool Add> inline PPCVRegister saturating(const PPCVRegister& a, const PPCVRegister& b, uint32_t& vscr) {
    PPCVRegister d{};
    for (size_t i = 0; i < 16 / sizeof(T); ++i) {
        int64_t x = lane<T>(a, i), y = lane<T>(b, i);
        put<T>(d, i, clamp_signed<T>(Add ? x + y : x - y, vscr));
    }
    return d;
}
template<class Out, class In> inline PPCVRegister pack(const PPCVRegister& a, const PPCVRegister& b, uint32_t& vscr) {
    PPCVRegister d{};
    constexpr size_t count = 16 / sizeof(In);
    for (size_t i = 0; i < count; ++i) {
        put<Out>(d, i, clamp_signed<Out>(int64_t(lane<In>(b, i)), vscr));
        put<Out>(d, i + count, clamp_signed<Out>(int64_t(lane<In>(a, i)), vscr));
    }
    return d;
}
enum class Shift { Left, LogicalRight, ArithmeticRight, RotateLeft };
template<class T, Shift Operation> inline PPCVRegister shift(const PPCVRegister& a, const PPCVRegister& b) {
    PPCVRegister d{};
    constexpr unsigned bits = sizeof(T) * 8;
    using U = std::make_unsigned_t<T>;
    for (size_t i = 0; i < 16 / sizeof(T); ++i) {
        U x = lane<U>(a, i);
        unsigned n = lane<U>(b, i) & (bits - 1);
        U result;
        if constexpr (Operation == Shift::Left) result = U(uint64_t(x) << n);
        if constexpr (Operation == Shift::LogicalRight) result = U(x >> n);
        if constexpr (Operation == Shift::ArithmeticRight) {
            result = U(x >> n);
            if (n && (x & (U(1) << (bits - 1))))
                result |= U(uint64_t(std::numeric_limits<U>::max()) << (bits - n));
        }
        if constexpr (Operation == Shift::RotateLeft)
            result = n ? U((uint64_t(x) << n) | (x >> (bits - n))) : x;
        put<U>(d, i, result);
    }
    return d;
}
inline PPCVRegister float_to_unsigned(const PPCVRegister& a, unsigned scale, uint32_t& vscr) {
    PPCVRegister d{};
    for (size_t i = 0; i < 4; ++i) {
        float x = std::ldexp(a.f32[i], int(scale));
        if (std::isnan(x)) d.u32[i] = 0;
        else if (x < 0) { d.u32[i] = 0; vscr |= 1; }
        else if (double(x) >= 4294967296.0) { d.u32[i] = UINT32_MAX; vscr |= 1; }
        else d.u32[i] = uint32_t(x);
    }
    return d;
}
inline uint64_t multiply_high_signed(int64_t a, int64_t b) {
    const __int128 product = __int128(a) * __int128(b);
    return uint64_t(static_cast<unsigned __int128>(product) >> 64);
}
}
