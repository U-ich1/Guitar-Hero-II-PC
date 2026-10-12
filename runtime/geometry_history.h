#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <vector>
#include "host_clip_mapping.h"

namespace gh2::temporal {
struct GeometryKey {
    uint64_t shader=0;
    uint32_t layer=0,index_base=0,index_count=0;
    std::vector<uint32_t> fetch_descriptors;
    uint64_t pixel_shader=0;
    std::array<uint32_t,6> pass_state{};
    auto operator<=>(const GeometryKey&)const=default;
};
struct GeometryState {
    HostClipMapping clip_mapping;
    struct LinearSnapshot {
        uint32_t vertex_offset=0,vertex_bytes=0,index_offset=0,index_bytes=0,index_size=0;
        uint32_t vertex_endian=0,index_endian=0;
        bool available=false;
        uint32_t base_vertex=0,index_min=0,index_max=0x00ffffff;
    } linear;
    // All VS float constants, including bone palettes. Geometry content itself
    // is not captured by this state and must have a verified previous buffer.
    std::array<uint32_t,1024> constants{};
    uint64_t geometry_revision=0;
    bool previous_vertices_preserved=false;
    uint64_t tile_identity=0;
};
class GeometryHistory {
    struct Entry{GeometryState state;bool ambiguous=false;std::vector<uint64_t> tiles;};
    std::map<GeometryKey,Entry> previous_,current_;
    bool overflow_=false;
public:
    static constexpr size_t kMaximumDraws=2048;
    void Record(GeometryKey key,GeometryState state) {
        if(current_.size()>=kMaximumDraws){overflow_=true;return;}
        auto found=current_.find(key);
        if(found==current_.end()){const auto tile=state.tile_identity;current_.emplace(std::move(key),Entry{std::move(state),false,{tile}});return;}
        auto& entry=found->second;
        bool repeated_tile=false;for(auto tile:entry.tiles)repeated_tile|=tile==state.tile_identity;
        if(repeated_tile||state.constants!=entry.state.constants||state.geometry_revision!=entry.state.geometry_revision)entry.ambiguous=true;
        else entry.tiles.push_back(state.tile_identity);
    }
    // Query only after the whole frame has been classified; a later duplicate
    // draw can invalidate an apparently unique earlier key.
    const GeometryState* Previous(const GeometryKey& key)const {
        if(overflow_)return nullptr;
        auto now=current_.find(key),before=previous_.find(key);
        if(now==current_.end()||before==previous_.end()||now->second.ambiguous||before->second.ambiguous||
           !now->second.state.previous_vertices_preserved||
           now->second.state.geometry_revision!=before->second.state.geometry_revision)return nullptr;
        return &before->second.state;
    }
    template<class Visitor>void ForEachPositionCandidate(Visitor&& visitor)const {
        if(overflow_)return;
        for(const auto& [key,now]:current_){
            auto before=previous_.find(key);
            if(!now.ambiguous&&before!=previous_.end()&&!before->second.ambiguous&&now.state.linear.available&&before->second.state.linear.available&&
               now.state.geometry_revision==before->second.state.geometry_revision)visitor(key,now.state,before->second.state);
        }
    }
    struct Statistics{size_t unique=0,ambiguous=0,valid_previous=0,tile_replays=0;bool overflow=false;};
    Statistics Stats()const {
        Statistics result;result.overflow=overflow_;
        for(const auto& [key,entry]:current_){if(entry.ambiguous)++result.ambiguous;else ++result.unique;if(Previous(key))++result.valid_previous;result.tile_replays+=entry.tiles.size()-1;}
        return result;
    }
    void EndFrame(bool reset=false) {
        if(reset||overflow_)previous_.clear();else previous_=std::move(current_);
        current_.clear();overflow_=false;
    }
    void Reset(){previous_.clear();current_.clear();overflow_=false;}
};
}
