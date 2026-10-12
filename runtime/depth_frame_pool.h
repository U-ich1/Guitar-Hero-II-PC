#pragma once
#include "depth_conversion.h"
#include <vector>

namespace gh2::temporal {
// Bounded slots owned by the renderer. Acquire never waits; in-flight slots
// cannot be overwritten. Submission IDs come from its existing GPU fence.
class DepthFramePool {
public:
    struct Slot {
        Microsoft::WRL::ComPtr<ID3D12Resource> output;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
        uint64_t submission=0;
        uint32_t width=0,height=0;
    };
private:
    std::array<Slot,12> slots_;
public:
    Slot* Acquire(ID3D12Device* device,uint64_t completed,uint64_t submission,
                  uint32_t width,uint32_t height,D3D12_CPU_DESCRIPTOR_HANDLE source_srv,ID3D12Resource* shared_output=nullptr) {
        if(!device||!submission||submission<=completed||!width||!height||!source_srv.ptr)return nullptr;
        Slot* slot=nullptr;
        for(auto& candidate:slots_)if(candidate.submission<=completed){
            bool referenced_in_flight=false;
            // A new image must not overwrite a referenced atlas. Appending a
            // tile to the SAME explicitly shared atlas may reuse a completed
            // descriptor lease; its older commands no longer read that heap.
            if(!shared_output||candidate.output.Get()!=shared_output)
                for(const auto& other:slots_)if(candidate.output&&candidate.output.Get()==other.output.Get()&&other.submission>completed)referenced_in_flight=true;
            if(!referenced_in_flight){slot=&candidate;break;}
        }
        if(!slot)return nullptr;
        if(shared_output){slot->output=shared_output;slot->width=width;slot->height=height;}
        else if(!slot->output||slot->width!=width||slot->height!=height) {
            D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=width;desc.Height=height;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.Format=DXGI_FORMAT_R32_FLOAT;desc.SampleDesc.Count=1;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            D3D12_HEAP_PROPERTIES props{};props.Type=D3D12_HEAP_TYPE_DEFAULT;
            Microsoft::WRL::ComPtr<ID3D12Resource> output;
            if(FAILED(device->CreateCommittedResource(&props,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&output))))return nullptr;
            slot->output=std::move(output);slot->width=width;slot->height=height;
        }
        if(!slot->heap){D3D12_DESCRIPTOR_HEAP_DESC desc{};desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;desc.NumDescriptors=2;desc.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            if(FAILED(device->CreateDescriptorHeap(&desc,IID_PPV_ARGS(&slot->heap))))return nullptr;}
        auto handle=slot->heap->GetCPUDescriptorHandleForHeapStart();
        device->CopyDescriptorsSimple(1,handle,source_srv,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        handle.ptr+=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=DXGI_FORMAT_R32_FLOAT;uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
        device->CreateUnorderedAccessView(slot->output.Get(),nullptr,&uav,handle);
        slot->submission=submission;return slot;
    }
    void ClearAfterGpuDrain(){for(auto& slot:slots_)slot=Slot{};}
};
}
