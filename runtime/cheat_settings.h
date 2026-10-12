#pragma once
#include <rex/cvar.h>
#include <atomic>
#include <charconv>
#include <cstdint>
#include "native_cheats.h"

namespace gh2::cheats {
// -1 preserves original cheat codes; 0 off; 1..5 progressive scroll speed.
inline std::atomic<int> hyperspeed{-1};
inline std::atomic<int> last_hyperspeed{-1};
inline auto registration=rex::cvar::FlagRegistrar({
    "gh2_hyperspeed",rex::cvar::FlagType::Int32,"Gameplay/Cheats","Hyperspeed: -1 original cheat codes, 0 off, levels 1-5",
    [](std::string_view text){
        int value=0;auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value);
        if(error!=std::errc{}||end!=text.data()+text.size()||value < -1||value > 5)return false;
        hyperspeed.store(value);return true;
    },[]{return std::to_string(hyperspeed.load());},{},rex::cvar::Lifecycle::kHotReload,{},"-1"});

// Called only after GH2's original Hyperspeed equality check in highway setup.
// Leave the original script evaluation and temporary-node cleanup intact.
inline uint64_t apply_hyperspeed(uint64_t native_result){
    const int mode=hyperspeed.load();
    const uint64_t result=mode < 0?native_result:uint64_t(mode>0);
    last_hyperspeed.store(mode<0?int((native_result&255)!=0):mode);
    return result;
}
inline float highway_multiplier(){const int level=last_hyperspeed.load();return level<=1?1.5f:1.5f+float(level-1)*0.25f;}
}
