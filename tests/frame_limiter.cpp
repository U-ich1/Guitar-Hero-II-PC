#include "../runtime/frame_limiter.h"
#include <iostream>
#include <stdexcept>
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
    using gh2::graphics::FrameLimiter;
    for(int limit:{1,30,60,73,120,144,240,1000,2147483647}){
        FrameLimiter limiter;int frames=0;
        for(int tick=0;tick<14400;++tick)if(limiter.allow(int64_t(tick)*1000000000LL/1440,limit))++frames;
        const int expected=std::min(limit,1440)*10;
        require(std::abs(frames-expected)<=1,"Custom cap drifted over a 10-second simulated run");
    }
    // Real repaint callbacks may arrive close to the target cadence, with jitter.
    for(int hz:{60,120,144}){
        FrameLimiter sparse;int admitted=0;
        for(int tick=0;tick<hz*10;++tick){
            const int64_t jitter=int64_t(tick%3-1)*500000;
            if(sparse.allow(int64_t(tick)*1000000000LL/hz+jitter,60))++admitted;
        }
        require(admitted>=590&&admitted<=601,"Near-target repaint cadence lost excessive frames");
    }
    FrameLimiter limiter;require(limiter.allow(0,60),"First frame delayed");require(!limiter.allow(1,60),"Immediate second frame admitted");
    require(limiter.allow(5000000000LL,60),"Stall recovery failed");require(!limiter.allow(5000000001LL,60),"Stall recovery queued a burst");
    require(limiter.allow(5000000002LL,30),"Live cap change did not reset its deadline");require(limiter.allow(5000000003LL,0)&&limiter.allow(5000000004LL,0),"Unlimited did not disable throttling");
    std::cout<<"PASS: custom integers, rate accuracy, unlimited, live changes and no catch-up bursts; no sleeps or game-clock access.\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
