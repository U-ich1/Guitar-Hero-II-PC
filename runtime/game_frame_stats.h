#pragma once
#include <atomic>
#include <chrono>
#include <algorithm>
#include <cfenv>
#include <cstdint>
namespace gh2::performance {
inline std::atomic<double> output_fps{0},worst_frame_ms{0},log_worst_frame_ms{0};
inline std::atomic<uint64_t> swap_calls{0},swap_total_ns{0},swap_max_ns{0};
inline void record_swap_duration(uint64_t nanoseconds){
    swap_calls.fetch_add(1,std::memory_order_relaxed);swap_total_ns.fetch_add(nanoseconds,std::memory_order_relaxed);
    auto maximum=swap_max_ns.load(std::memory_order_relaxed);
    while(maximum<nanoseconds&&!swap_max_ns.compare_exchange_weak(maximum,nanoseconds,std::memory_order_relaxed)){}
}
class SwapScope {
    std::chrono::steady_clock::time_point start_=std::chrono::steady_clock::now();
public:
    ~SwapScope(){record_swap_duration(uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start_).count()));}
};
// VdSwap is recorded in the executable's import adapter, not in the UI DLL.
// It measures guest output submissions rather than repeated overlay redraws.
inline void record_output(){
    using Clock=std::chrono::steady_clock;
    static std::atomic_flag busy=ATOMIC_FLAG_INIT;
    if(busy.test_and_set(std::memory_order_acquire))return;
    std::fenv_t environment;std::feholdexcept(&environment);
    static auto start=Clock::now(),previous=start;
    static unsigned count=0;static double worst=0;static bool has_previous=false;
    const auto now=Clock::now();
    if(has_previous){
        const double interval=std::chrono::duration<double,std::milli>(now-previous).count();
        worst=std::max(worst,interval);
        double peak=log_worst_frame_ms.load();
        while(peak<interval&&!log_worst_frame_ms.compare_exchange_weak(peak,interval)){}
    }
    has_previous=true;
    previous=now;++count;const double elapsed=std::chrono::duration<double>(now-start).count();
    if(elapsed>=1){output_fps.store(double(count)/elapsed);worst_frame_ms.store(worst);start=now;count=0;worst=0;}
    std::fesetenv(&environment);busy.clear(std::memory_order_release);
}
}
