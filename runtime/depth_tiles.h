#pragma once
#include "depth_conversion.h"
#include <algorithm>
#include <optional>
#include <vector>
namespace gh2::temporal {
struct Rectangle {uint32_t left,top,width,height;};
inline std::optional<DepthRegion> PlanDepthTile(Rectangle viewport,Rectangle scissor,
        uint64_t allocation_width,uint32_t allocation_height,int32_t window_x,int32_t window_y,
        uint32_t scale_x,uint32_t scale_y,uint32_t output_width,uint32_t output_height,float depth_scale,float depth_offset) {
    const uint64_t left=std::max(viewport.left,scissor.left),top=std::max(viewport.top,scissor.top);
    const uint64_t right=std::min({uint64_t(viewport.left)+viewport.width,uint64_t(scissor.left)+scissor.width,allocation_width});
    const uint64_t bottom=std::min({uint64_t(viewport.top)+viewport.height,uint64_t(scissor.top)+scissor.height,uint64_t(allocation_height)});
    if(right<=left||bottom<=top||!scale_x||!scale_y)return {};
    const int64_t destination_x=int64_t(left)-int64_t(window_x)*scale_x;
    const int64_t destination_y=int64_t(top)-int64_t(window_y)*scale_y;
    if(destination_x<0||destination_y<0||uint64_t(destination_x)+right-left>output_width||uint64_t(destination_y)+bottom-top>output_height)return {};
    return DepthRegion{uint32_t(left),uint32_t(top),uint32_t(right-left),uint32_t(bottom-top),depth_scale,depth_offset,uint32_t(destination_x),uint32_t(destination_y)};
}
inline bool CompleteDepthTiles(const std::vector<DepthRegion>& regions,uint32_t width,uint32_t height) {
    uint64_t area=0;
    for(size_t i=0;i<regions.size();++i) {
        const auto& a=regions[i];
        if(uint64_t(a.destination_left)+a.width>width||uint64_t(a.destination_top)+a.height>height)return false;
        area+=uint64_t(a.width)*a.height;
        for(size_t j=0;j<i;++j){const auto& b=regions[j];
            if(uint64_t(a.destination_left)<uint64_t(b.destination_left)+b.width&&uint64_t(b.destination_left)<uint64_t(a.destination_left)+a.width&&
               uint64_t(a.destination_top)<uint64_t(b.destination_top)+b.height&&uint64_t(b.destination_top)<uint64_t(a.destination_top)+a.height)return false;}
    }
    return width&&height&&area==uint64_t(width)*height;
}
}
