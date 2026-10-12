#include "temporal_capture.h"
#include <cassert>
#include <cfenv>
#include <iostream>

int main(int argc,char** argv) {
    assert(argc==2);
    const auto root=std::filesystem::u8path(argv[1]);
    std::filesystem::create_directories(root);
    gh2::temporal::Mailbox mailbox;
    assert(!mailbox.Request(root/"wrong.txt"));assert(!mailbox.Take());
    std::jthread sender([&]{assert(mailbox.Request(root/"complete.jsonl"));});sender.join();
    assert(!mailbox.Request(root/"second.jsonl"));
    auto path=mailbox.Take();assert(path&&*path==root/"complete.jsonl");assert(!mailbox.Take());
    gh2::temporal::Draw draw;
    draw.vertex_shader=0xfedcba9876543210ULL;draw.pixel_shader=2;
    draw.depth_control=6;draw.depth_info=9;draw.vertices=12;draw.host_targets=true;
    draw.depth_width=2560;draw.depth_height=1440;draw.depth_samples=2;
    draw.constants.push_back({3,{0x7fc12345,0x80000000,0x3f800000,0}});
    draw.fetches.push_back({4,{123,456}});
    std::fesetround(FE_DOWNWARD);std::feclearexcept(FE_ALL_EXCEPT);std::feraiseexcept(FE_INVALID);
    {
        gh2::temporal::Capture capture;
        assert(!capture.WantsDraw());capture.Swap(640,360);
        assert(capture.Start(*path));assert(!capture.Start(root/"busy.jsonl"));
        for(size_t frame=0;frame<gh2::temporal::Capture::kFrames;++frame) {
            for(size_t i=0;i<gh2::temporal::Capture::kDraws+7;++i) {
                if(capture.WantsDraw())capture.AddDraw(draw);
            }
            capture.Swap(1280,720);
        }
        assert(!capture.Active());assert(!capture.WantsDraw());
    }
    assert(std::fegetround()==FE_DOWNWARD);assert(std::fetestexcept(FE_INVALID));
    {
        gh2::temporal::Capture partial;assert(partial.Start(root/"partial.jsonl"));
        assert(partial.WantsDraw());partial.AddDraw(draw);partial.Swap(1280,720);
    }
    {
        std::atomic<int> status=0;
        std::ofstream blocker(root/"blocked");blocker<<"file";blocker.close();
        gh2::temporal::Write(root/"blocked"/"failure.jsonl",{},true,status);
        assert(status.load()==-1);
    }
    std::cout<<"PASS: inactive path, cross-thread request, frame boundaries, bounded draws/frames, background write, partial shutdown, write failure and FP environment.\n";
}
