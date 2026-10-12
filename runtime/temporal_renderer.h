#pragma once
#include "depth_frame_pool.h"
#include "deferred_depth.h"
#include "geometry_history.h"
#include "vertex_frame_pool.h"
#include "depth_tiles.h"
#include "linear_motion_clips.h"
#include "clip_frame_pool.h"
#include "motion_raster.h"
#include "motion_frame_pool.h"
#include "deferred_motion.h"
#include <atomic>
#include <bit>
#include <rex/graphics/util/draw.h>
namespace gh2::temporal {
inline std::atomic<unsigned> renderer_probe_frames{0};
inline unsigned Layer(uint32_t depth_scale_bits) {
    const float value=std::bit_cast<float>(depth_scale_bits);
    if(std::abs(value+0.9f)<0.00001f)return 1;
    if(std::abs(value+0.1f)<0.00001f)return 2;
    if(std::abs(value+0.02f)<0.00001f)return 3;
    return 0;
}
struct RendererInputs {
    DepthConversion converter;
    DepthFramePool pool;
    GeometryHistory history;
    VertexFramePool vertices;
    LinearMotionClips linear_positions;
    ClipFramePool clip_pool;
    bool positions_initialized=false;
    MotionRaster motion_raster;
    MotionFramePool motion_pool;
    bool motion_initialized=false;
    rex::graphics::draw_util::ViewportInfo viewport{};
    rex::graphics::draw_util::Scissor scissor{};
    int32_t window_x=0,window_y=0;
    uint32_t scale_x=1,scale_y=1,width=0,height=0;
    unsigned frames=0,last_layer=0;
    bool initialized=false;
    std::array<bool,4> depth_recorded{};
    std::array<DepthFramePool::Slot*,4> depth_atlas{};
    std::array<std::vector<uint64_t>,4> depth_tiles{};
    std::array<std::vector<DepthRegion>,4> depth_regions{};
    void BeginFrame() {
        if(const unsigned requested=renderer_probe_frames.exchange(0)){
            frames=requested;history.Reset();vertices.EndFrame(true);last_layer=0;
        }
        depth_recorded.fill(false);
        depth_atlas.fill(nullptr);for(auto& tiles:depth_tiles)tiles.clear();
        for(auto& regions:depth_regions)regions.clear();
    }
};
}
