#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
namespace gh2::graphics {
class FrameLimiter {
    int limit_=0;
    int64_t next_=0;
public:
    // Nonblocking presentation admission. Paint mode serializes callers.
    // Missed deadlines discard old presentation slots, never queue a burst.
    bool allow(int64_t now,int limit){
        if(limit<=0){limit_=0;next_=0;return true;}
        const int64_t period=(1000000000LL+int64_t(limit)-1)/int64_t(limit);
        if(limit!=limit_){limit_=limit;next_=now+period;return true;}
        if(now<next_)return false;
        next_+=period;
        if(next_<=now)next_=now+period;
        return true;
    }
};
inline int64_t presentation_time_ns(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
}
