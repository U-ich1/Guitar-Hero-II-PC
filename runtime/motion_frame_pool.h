#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
namespace gh2::temporal {
class MotionFramePool {
public:
    struct Slot {
        std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,2> textures;
        Microsoft::WRL::ComPtr<ID3D12Resource> depth;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvs,depth_heap;
        uint64_t submission=0;uint32_t width=0,height=0;
        bool certified=false;
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE,2> Handles(ID3D12Device* device)const {
            auto a=rtvs->GetCPUDescriptorHandleForHeapStart(),b=a;b.ptr+=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);return {a,b};
        }
    };
private:
    std::array<Slot,6> slots_;
public:
    Slot* Acquire(ID3D12Device* device,uint64_t completed,uint64_t submission,uint32_t width,uint32_t height,ID3D12Resource* depth) {
        if(!device||!depth||!width||!height||submission<=completed)return nullptr;
        const auto source=depth->GetDesc();
        if(source.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||source.Format!=DXGI_FORMAT_R32_FLOAT||source.SampleDesc.Count!=1||source.Width<width||source.Height<height)return nullptr;
        Slot* slot=nullptr;for(auto& s:slots_)if(s.submission<=completed){slot=&s;break;}if(!slot)return nullptr;
        if(!slot->textures[0]||slot->width!=width||slot->height!=height){
            D3D12_HEAP_PROPERTIES props{};props.Type=D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=width;desc.Height=height;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,2> textures;
            for(unsigned i=0;i<2;++i){desc.Format=i?DXGI_FORMAT_R8_UINT:DXGI_FORMAT_R16G16_FLOAT;if(FAILED(device->CreateCommittedResource(&props,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&textures[i]))))return nullptr;}
            slot->textures=std::move(textures);slot->width=width;slot->height=height;
        }
        if(!slot->rtvs){D3D12_DESCRIPTOR_HEAP_DESC desc{};desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;desc.NumDescriptors=2;if(FAILED(device->CreateDescriptorHeap(&desc,IID_PPV_ARGS(&slot->rtvs))))return nullptr;}
        if(!slot->depth_heap){D3D12_DESCRIPTOR_HEAP_DESC desc{};desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;desc.NumDescriptors=1;desc.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;if(FAILED(device->CreateDescriptorHeap(&desc,IID_PPV_ARGS(&slot->depth_heap))))return nullptr;}
        auto handles=slot->Handles(device);for(unsigned i=0;i<2;++i)device->CreateRenderTargetView(slot->textures[i].Get(),nullptr,handles[i]);
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=DXGI_FORMAT_R32_FLOAT;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
        device->CreateShaderResourceView(depth,&srv,slot->depth_heap->GetCPUDescriptorHandleForHeapStart());
        slot->depth=depth;slot->submission=submission;slot->certified=false;return slot;
    }
    void ClearAfterGpuDrain(){for(auto& s:slots_)s=Slot{};}
};
}
