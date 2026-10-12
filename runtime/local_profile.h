#pragma once
#include <atomic>
#include <cstdint>
namespace gh2::profile {
// One offline PC profile, backed by the existing runtime XUID/save directory.
// Its controller port can change without creating another save identity.
inline std::atomic<unsigned> signed_in_player{0}, last_button_player{0};
inline void observe_buttons(unsigned player,uint16_t current,uint16_t previous) {
    if(player<4&&(current&uint16_t(~previous)))last_button_player.store(player);
}
inline uint32_t runtime_user(uint32_t user) {
    if(user>=4)return user; // Preserve the SDK's invalid-user behavior.
    return user==signed_in_player.load()?0:1;
}
inline unsigned sign_in() {
    const unsigned user=last_button_player.load();
    const unsigned old=signed_in_player.exchange(user<4?user:0);
    return (1U<<old)|(1U<<signed_in_player.load());
}
}
