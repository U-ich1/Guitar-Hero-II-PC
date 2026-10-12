#pragma once
#include <rex/cvar.h>
#include <rex/memory.h>
#include <atomic>
#include <array>
#include <vector>
#include <charconv>
#include <string_view>
namespace gh2::cheats {
// Supported GH2 image's global DataVariable tree (not arbitrary Symbol maps).
inline constexpr uint32_t VariableMap=0x827FE6D4;
struct NativeCheat {const char* key;const char* variable;int maximum;std::atomic<int> mode{-1};std::atomic<uint32_t> node{0};};
inline std::array<NativeCheat,5> native{{
    {"gh2_cheat_unlock_all","gh2_pc_unlock_all",1},
    {"gh2_cheat_air_guitar","cheat_guitar",1},
    {"gh2_cheat_performance","cheat_enabled_performance_mode",1},
    {"gh2_cheat_flaming_head","cheat_enabled_flaming_head",1},
    {"gh2_cheat_crowd_heads","cheat_crowd_heads",2}}};
inline std::array<std::atomic<uint32_t>,3> symbols{};
inline auto native_registration=[]{
    std::vector<rex::cvar::FlagRegistrar> flags;
    for(auto& cheat:native){auto* setting=&cheat;
        flags.emplace_back(rex::cvar::FlagEntry{setting->key,rex::cvar::FlagType::Int32,"Gameplay/Cheats","-1 original cheat codes; 0 off; positive enabled variant",
            [setting](std::string_view s){int v;auto [end,err]=std::from_chars(s.data(),s.data()+s.size(),v);if(err!=std::errc{}||end!=s.data()+s.size()||v < -1||v>setting->maximum)return false;setting->mode.store(v);return true;},
            [setting]{return std::to_string(setting->mode.load());},{},rex::cvar::Lifecycle::kHotReload,{},"-1"});
    }return flags;
}();
template<class Base> inline void observe_symbol(uint32_t text,uint32_t symbol,Base base){
    if(!text||!symbol)return;
    const std::string_view name(reinterpret_cast<const char*>(base+text));
    constexpr std::array<std::string_view,3> names{"air_guitar","eyeball_crowd_heads","monkey_crowd_heads"};
    for(unsigned i=0;i<names.size();++i)if(name==names[i])symbols[i].store(symbol);
}
template<class Base> inline void apply_native(Base base){
    for(unsigned i=0;i<native.size();++i){auto& cheat=native[i];const int mode=cheat.mode.load();const uint32_t node=cheat.node.load();
        if(!node||(mode<0&&i!=0))continue;
        uint32_t value=uint32_t(mode),type=0;
        if((i==1||i==4)&&mode){value=symbols[i==1?0:unsigned(mode)].load();if(!value)continue;type=5;}
        rex::memory::store_and_swap<uint32_t>(base+node,value);
        rex::memory::store_and_swap<uint32_t>(base+(node+4),type);
    }
}
template<class Base> inline void observe_variable(uint32_t symbol,uint32_t node,Base base){
    if(!symbol||!node)return;
    const std::string_view name(reinterpret_cast<const char*>(base+symbol));
    for(auto& cheat:native)if(name==cheat.variable){cheat.node.store(node);apply_native(base);break;}
}
}

