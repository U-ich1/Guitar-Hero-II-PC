#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
namespace gh2::temporal {
class ClipFramePool {
public:
    static constexpr uint32_t kMaximumVertices=16384;
    struct Slot {
        Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
        uint64_t submission=0;uint32_t capacity=0;
        D3D12_RESOURCE_STATES state=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    };
private:
    std::array<Slot,48> slots_;
public:
    Slot* Acquire(ID3D12Device* device,uint64_t completed,uint64_t submission,ID3D12Resource* current,ID3D12Resource* previous,uint32_t count) {
        if(!count||count>kMaximumVertices||!current||!previous||submission<=completed)return nullptr;
        Slot* slot=nullptr;for(auto& candidate:slots_)if(candidate.submission<=completed){slot=&candidate;break;}if(!slot)return nullptr;
        if(!slot->buffer||slot->capacity<count){D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=uint64_t(count)*36;desc.Height=1;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;D3D12_HEAP_PROPERTIES props{};props.Type=D3D12_HEAP_TYPE_DEFAULT;
            Microsoft::WRL::ComPtr<ID3D12Resource> buffer;if(FAILED(device->CreateCommittedResource(&props,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&buffer))))return nullptr;
            slot->buffer=std::move(buffer);slot->capacity=count;slot->state=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;}
        if(!slot->heap){D3D12_DESCRIPTOR_HEAP_DESC desc{};desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;desc.NumDescriptors=3;desc.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;if(FAILED(device->CreateDescriptorHeap(&desc,IID_PPV_ARGS(&slot->heap))))return nullptr;}
        auto handle=slot->heap->GetCPUDescriptorHandleForHeapStart();const auto step=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        for(auto* source:{current,previous}){D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;srv.Format=DXGI_FORMAT_R32_TYPELESS;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Buffer.NumElements=UINT(source->GetDesc().Width/4);srv.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW;device->CreateShaderResourceView(source,&srv,handle);handle.ptr+=step;}
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;uav.Format=DXGI_FORMAT_R32_TYPELESS;uav.Buffer.NumElements=slot->capacity*9;uav.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_RAW;device->CreateUnorderedAccessView(slot->buffer.Get(),nullptr,&uav,handle);
        slot->submission=submission;return slot;
    }
    void ClearAfterGpuDrain(){for(auto& slot:slots_)slot=Slot{};}
};
}
