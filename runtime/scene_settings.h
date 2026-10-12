#pragma once
#include <rex/cvar.h>
#include <atomic>
#include <cstdint>
namespace gh2::scene {
inline std::atomic_bool no_audience{false};
inline std::atomic<uint64_t> crowd_draws_skipped{0};
inline auto registration=rex::cvar::FlagRegistrar({
    "gh2_no_audience",rex::cvar::FlagType::Boolean,"Graphics/Scene","Hide audience geometry; crowd audio remains enabled",
    [](std::string_view text){if(text!="true"&&text!="false"&&text!="1"&&text!="0")return false;
        no_audience.store(text=="true"||text=="1",std::memory_order_relaxed);return true;},
    []{return no_audience.load(std::memory_order_relaxed)?"true":"false";},{},rex::cvar::Lifecycle::kHotReload,{},"false"});
inline bool skip_crowd_draw() {
    if(!no_audience.load(std::memory_order_relaxed))return false;
    crowd_draws_skipped.fetch_add(1,std::memory_order_relaxed);return true;
}
}
