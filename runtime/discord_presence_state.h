#pragma once
#include <windows.h>
#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <string_view>
#include <cstring>
#include <bit>
#include <algorithm>
#include <cctype>
namespace gh2::discord {
inline constexpr char ApplicationId[]="1559050915195912242";
struct Snapshot {int status=0;std::string mode,song,difficulty,venue;};
inline std::mutex snapshot_mutex;
inline Snapshot snapshot;
inline std::array<std::atomic<uint32_t>,5> nodes{};
inline std::array<std::atomic<uint32_t>,5> observed_symbols{};
inline constexpr std::array<std::string_view,5> variable_names{"gh2_rpc_status","gh2_rpc_mode","gh2_rpc_song","gh2_rpc_difficulty","gh2_rpc_venue"};
inline bool readable(const void* pointer,size_t bytes) {
    MEMORY_BASIC_INFORMATION region{};
    return pointer&&VirtualQuery(pointer,&region,sizeof(region))&&region.State==MEM_COMMIT&&
        !(region.Protect&(PAGE_NOACCESS|PAGE_GUARD))&&uintptr_t(pointer)>=uintptr_t(region.BaseAddress)&&
        bytes<=region.RegionSize-(uintptr_t(pointer)-uintptr_t(region.BaseAddress));
}
template<class Base>std::string guest_string(Base base,uint32_t address) {
    if(!address)return {};
    std::string result;result.reserve(128);
    for(unsigned i=0;i<128;++i){if(address>UINT32_MAX-i||!readable(base+(address+i),1))return {};
        char c=*reinterpret_cast<const char*>(base+(address+i));if(!c)return result;result+=c;}
    return {};
}
template<class Base>void observe_variable(uint32_t symbol,uint32_t node,Base base) {
    if(!symbol||!node)return;
    // Native Symbol strings are interned and immutable for this game session.
    // Update known variable nodes directly; cache unrelated interned names in
    // a bounded per-thread table so ordinary lookups avoid VirtualQuery.
    for(unsigned i=0;i<nodes.size();++i)if(observed_symbols[i].load(std::memory_order_relaxed)==symbol){nodes[i].store(node);return;}
    static thread_local std::array<uint32_t,512> unrelated{};
    auto& cached=unrelated[(symbol>>2)&511];if(cached==symbol)return;
    if(!readable(base+symbol,8))return;
    if(std::memcmp(base+symbol,"gh2_rpc_",8)){cached=symbol;return;}
    const auto name=guest_string(base,symbol);
    for(unsigned i=0;i<nodes.size();++i)if(name==variable_names[i]){observed_symbols[i].store(symbol,std::memory_order_relaxed);nodes[i].store(node);return;}
    cached=symbol;
}
template<class Base>void sample(Base base) {
    // Called on the guest swap thread. Never wait for the IPC worker.
    static std::atomic<ULONGLONG> next{0};const auto now=GetTickCount64();auto due=next.load();
    if(now<due||!next.compare_exchange_strong(due,now+500))return;
    Snapshot value;
    for(unsigned i=0;i<nodes.size();++i){const auto address=nodes[i].load();if(!address||!readable(base+address,8))return;
        uint32_t raw,type;std::memcpy(&raw,base+address,4);std::memcpy(&type,base+(address+4),4);
        raw=std::byteswap(raw);type=std::byteswap(type);
        if(!i){if(type!=0||raw>3)return;value.status=int(raw);}
        else {if(type!=5)return;auto text=guest_string(base,raw);
            if(i==1)value.mode=std::move(text);else if(i==2)value.song=std::move(text);else if(i==3)value.difficulty=std::move(text);else value.venue=std::move(text);}
    }
    if(snapshot_mutex.try_lock()){snapshot=std::move(value);snapshot_mutex.unlock();}
}
inline std::string json_quote(std::string_view input) {
    std::string out="\"";constexpr char hex[]="0123456789abcdef";
    // Keep complete UTF-8 code points within Discord's text limit.
    size_t length=std::min(size_t(120),input.size());
    while(length<input.size()&&length&&((uint8_t(input[length])&0xc0)==0x80))--length;
    for(unsigned char c:input.substr(0,length)) {
        if(c=='"'||c=='\\'){out+='\\';out+=char(c);}
        else if(c<32){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}
        else out+=char(c);
    }
    return out+'"';
}
inline std::string mode_name(std::string_view mode) {
    if(mode=="career")return "Career";if(mode=="quickplay")return "Quick Play";
    if(mode=="practice")return "Practice";if(mode=="multi_coop")return "Co-op";
    if(mode=="multi_vs")return "Face-Off";if(mode=="multi_fo")return "Pro Face-Off";
    if(mode=="tutorial")return "Tutorial";if(mode=="multiplayer")return "Multiplayer";
    return "Menus";
}
inline std::string activity(const Snapshot& s,std::string_view title) {
    std::string details="In menus",state=mode_name(s.mode);
    if(s.status){details=title.empty()?"Playing a song":std::string(title);
        if(!s.difficulty.empty()&&s.difficulty!="none") {auto difficulty=s.difficulty;
            if(difficulty.starts_with("kDifficulty"))difficulty.erase(0,11);
            difficulty[0]=char(std::toupper(uint8_t(difficulty[0])));state+=" | "+difficulty;}
        if(s.status==2)state+=" | Paused";if(s.status==3)state+=" | Results";}
    std::string asset="gh2_logo",venue="Guitar Hero 2";
    constexpr std::array<std::pair<std::string_view,std::string_view>,8> venues{{
        {"battle","Battle of the Bands"},{"small1","The Rat Cellar"},{"small2","The Blackout Bar"},
        {"big","RedOctane Club"},{"theatre","Rock City Theater"},{"fest","Vans Warped Tour"},
        {"arena","The Arena"},{"stone","Stonehenge"}}};
    if(s.status)for(auto [key,name]:venues)if(s.venue==key){asset="venue_"+std::string(key);venue=name;break;}
    return "{\"details\":"+json_quote(details)+",\"state\":"+json_quote(state)+
        ",\"assets\":{\"large_image\":"+json_quote(asset)+",\"large_text\":"+json_quote(venue)+"},\"instance\":false}";
}
inline std::string command(std::string_view body,uint32_t pid,uint64_t nonce) {
    return "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":"+std::to_string(pid)+",\"activity\":"+std::string(body)+"},\"nonce\":\""+std::to_string(nonce)+"\"}";
}
}
