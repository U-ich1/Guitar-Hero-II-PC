#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
#include <map>
#include <optional>
namespace gh2::temporal {
class VertexFramePool {
public:
    static constexpr uint32_t kCapacity=16*1024*1024;
    struct Range {uint32_t source,size,offset;};
    struct Slot {
        Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
        std::map<std::pair<uint32_t,uint32_t>,Range> ranges;
        uint64_t submission=0;
        uint32_t used=0;
        D3D12_RESOURCE_STATES state=D3D12_RESOURCE_STATE_COPY_DEST;
    };
private:
    std::array<Slot,4> slots_;
    Slot *active_=nullptr,*previous_=nullptr;
public:
    Slot* Begin(ID3D12Device* device,uint64_t completed) {
        if(active_)return active_;
        for(auto& slot:slots_)if(&slot!=previous_&&slot.submission<=completed) {
            if(!slot.buffer){D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC desc{};
                desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=kCapacity;desc.Height=1;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&slot.buffer))))return nullptr;}
            slot.ranges.clear();slot.used=0;active_=&slot;return active_;
        }
        return nullptr;
    }
    std::optional<Range> Reserve(uint32_t source,uint32_t size,uint64_t submission,bool& copy_needed) {
        copy_needed=false;
        if(!active_||!size||!submission||uint64_t(source)+size>512ULL*1024*1024)return {};
        auto found=active_->ranges.find({source,size});
        if(found!=active_->ranges.end())return found->second;
        const uint32_t offset=(active_->used+15)&~uint32_t(15);
        if(uint64_t(offset)+size>kCapacity)return {};
        Range range{source,size,offset};active_->ranges.emplace(std::make_pair(source,size),range);
        active_->used=offset+size;active_->submission=submission;copy_needed=true;return range;
    }
    std::optional<Range> Previous(uint32_t source,uint32_t size)const {
        if(!previous_)return {};
        auto found=previous_->ranges.find({source,size});
        return found==previous_->ranges.end()?std::optional<Range>{}:found->second;
    }
    // Preserve bytes at a particular draw, rather than assuming a physical
    // range is immutable between draws. Tile replay association happens later.
    std::optional<Range> ReserveDraw(uint32_t source,uint32_t size,uint64_t submission) {
        if(!active_||!size||!submission||uint64_t(source)+size>512ULL*1024*1024)return {};
        const uint32_t offset=(active_->used+15)&~uint32_t(15);
        if(uint64_t(offset)+size>kCapacity)return {};
        active_->used=offset+size;active_->submission=submission;return Range{source,size,offset};
    }
    Slot* Active()const{return active_;}
    Slot* PreviousSlot()const{return previous_;}
    void EndFrame(bool reset=false){previous_=reset?nullptr:active_;active_=nullptr;}
    void ClearAfterGpuDrain(){active_=previous_=nullptr;for(auto& slot:slots_)slot=Slot{};}
};
}
