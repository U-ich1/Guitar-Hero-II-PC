#pragma once
#include "../runtime/dlss_dispatch.h"
#include <vector>
#include <cstring>
#include <cmath>

struct SyntheticResult { const char* mode;int result;uint32_t width,height;bool output_written; };
inline std::vector<SyntheticResult> TestDlssReconstruction(HMODULE dll,ID3D12Device* device) {
    using Microsoft::WRL::ComPtr;
    auto require=[](HRESULT hr){if(FAILED(hr))throw std::runtime_error("DLSS fixture D3D12 operation failed");};
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC queue_desc{};
    require(device->CreateCommandQueue(&queue_desc,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12Fence> fence;require(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)throw std::runtime_error("Fence event failed");
    struct EventGuard{HANDLE event;~EventGuard(){CloseHandle(event);}}event_guard{event};
    uint64_t submission=0;uint32_t frame=1000;
    const std::array<std::pair<const char*,sl::DLSSMode>,4> modes={{{"performance",sl::DLSSMode::eMaxPerformance},{"balanced",sl::DLSSMode::eBalanced},{"quality",sl::DLSSMode::eMaxQuality},{"native_aa",sl::DLSSMode::eDLAA}}};
    std::vector<SyntheticResult> results;
    gh2::dlss::Dispatch dispatch(dll,17);
    auto identity=[](){sl::float4x4 matrix;for(unsigned i=0;i<4;++i)matrix[i]={float(i==0),float(i==1),float(i==2),float(i==3)};return matrix;};
    for(auto mode:modes){
        sl::DLSSOptimalSettings optimal;
        if(dispatch.Optimal(mode.second,960,540,optimal)!=sl::Result::eOk)throw std::runtime_error("Synthetic DLSS dimensions failed");
        const uint32_t width=optimal.optimalRenderWidth,height=optimal.optimalRenderHeight;
        std::array<ComPtr<ID3D12Resource>,4> resources;
        const std::array<DXGI_FORMAT,4> formats={DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R16G16_FLOAT,DXGI_FORMAT_R16G16B16A16_FLOAT};
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        for(unsigned i=0;i<4;++i){
            D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width=i==3?960:width;desc.Height=i==3?540:height;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.Format=formats[i];desc.SampleDesc.Count=1;
            desc.Flags=i==3?D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS:D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            require(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,i==3?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&resources[i])));
        }
        ComPtr<ID3D12DescriptorHeap> rtvs;D3D12_DESCRIPTOR_HEAP_DESC descriptors{};descriptors.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;descriptors.NumDescriptors=3;
        require(device->CreateDescriptorHeap(&descriptors,IID_PPV_ARGS(&rtvs)));
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE,3> handles;
        auto handle=rtvs->GetCPUDescriptorHandleForHeapStart();const UINT stride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        for(unsigned i=0;i<3;++i){handles[i]=handle;device->CreateRenderTargetView(resources[i].Get(),nullptr,handle);handle.ptr+=stride;}
        ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> commands;
        require(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
        require(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&commands)));
        const float color[]={0.2f,0.4f,0.6f,1},depth[]={0.5f,0,0,0},motion[]={0,0,0,0};
        commands->ClearRenderTargetView(handles[0],color,0,nullptr);commands->ClearRenderTargetView(handles[1],depth,0,nullptr);commands->ClearRenderTargetView(handles[2],motion,0,nullptr);
        std::array<D3D12_RESOURCE_BARRIER,3> barriers{};
        constexpr auto read=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        for(unsigned i=0;i<3;++i){barriers[i].Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barriers[i].Transition={resources[i].Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_RENDER_TARGET,read};}
        commands->ResourceBarrier(3,barriers.data());
        sl::Constants constants;
        constants.cameraViewToClip=constants.clipToCameraView=constants.clipToLensClip=constants.clipToPrevClip=constants.prevClipToClip=identity();
        constants.jitterOffset={0,0};constants.mvecScale={1.0f/width,1.0f/height};constants.cameraPinholeOffset={0,0};
        constants.cameraPos={0,0,0};constants.cameraUp={0,1,0};constants.cameraRight={1,0,0};constants.cameraFwd={0,0,1};
        constants.cameraNear=0.1f;constants.cameraFar=1000;constants.cameraFOV=1;constants.cameraAspectRatio=960.0f/540;
        constants.depthInverted=sl::Boolean::eFalse;constants.cameraMotionIncluded=sl::Boolean::eTrue;constants.motionVectors3D=sl::Boolean::eFalse;constants.reset=sl::Boolean::eTrue;
        gh2::dlss::Inputs inputs{resources[0].Get(),resources[1].Get(),resources[2].Get(),resources[3].Get(),width,height,960,540,read,read,read,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,false,true,true};
        if(dispatch.Evaluate(inputs,constants,mode.second,frame++,commands.Get())!=sl::Result::eErrorInvalidIntegration)throw std::runtime_error("Unmatched frame accepted");
        inputs.matching_frame=true;
        auto invalid=inputs;invalid.object_motion_valid=false;
        if(gh2::dlss::Validate(invalid))throw std::runtime_error("Missing object motion accepted");
        invalid=inputs;invalid.depth_valid=false;
        if(gh2::dlss::Validate(invalid))throw std::runtime_error("Unverified depth accepted");
        invalid=inputs;invalid.output=invalid.color;
        if(gh2::dlss::Validate(invalid))throw std::runtime_error("Aliased input/output accepted");
        invalid=inputs;invalid.motion_state=UINT_MAX;
        if(gh2::dlss::Validate(invalid))throw std::runtime_error("Unknown resource state accepted");
        invalid=inputs;invalid.width=width+10000;
        if(gh2::dlss::Validate(invalid))throw std::runtime_error("Invalid resource extent accepted");
        invalid=inputs;invalid.motion=invalid.depth;
        if(gh2::dlss::Validate(invalid))throw std::runtime_error("Invalid motion format accepted");
        if(mode.second!=sl::DLSSMode::eDLAA && dispatch.Evaluate(inputs,constants,sl::DLSSMode::eDLAA,frame++,commands.Get())!=sl::Result::eErrorInvalidParameter)throw std::runtime_error("DLAA at scaled input accepted");
        auto result=dispatch.Evaluate(inputs,constants,mode.second,frame++,commands.Get());
        D3D12_RESOURCE_DESC output_desc=resources[3]->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT rows=0;UINT64 bytes_per_row=0,total=0;
        device->GetCopyableFootprints(&output_desc,0,1,0,&footprint,&rows,&bytes_per_row,&total);
        D3D12_RESOURCE_DESC readback_desc{};readback_desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;readback_desc.Width=total;readback_desc.Height=1;readback_desc.DepthOrArraySize=1;readback_desc.MipLevels=1;readback_desc.SampleDesc.Count=1;readback_desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        heap.Type=D3D12_HEAP_TYPE_READBACK;ComPtr<ID3D12Resource> readback;
        require(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&readback_desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)));
        D3D12_RESOURCE_BARRIER output_barrier{};output_barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;output_barrier.Transition={resources[3].Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};
        commands->ResourceBarrier(1,&output_barrier);
        D3D12_TEXTURE_COPY_LOCATION dest{};dest.pResource=readback.Get();dest.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dest.PlacedFootprint=footprint;
        D3D12_TEXTURE_COPY_LOCATION source{};source.pResource=resources[3].Get();source.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        commands->CopyTextureRegion(&dest,0,0,0,&source,nullptr);require(commands->Close());
        ID3D12CommandList* list=commands.Get();queue->ExecuteCommandLists(1,&list);require(queue->Signal(fence.Get(),++submission));
        require(fence->SetEventOnCompletion(submission,event));
        if(WaitForSingleObject(event,30000)!=WAIT_OBJECT_0)throw std::runtime_error("Synthetic DLSS GPU completion timeout");
        void* data=nullptr;D3D12_RANGE range{0,size_t(total)};require(readback->Map(0,&range,&data));
        const auto* pixel=reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(data)+footprint.Offset+270*footprint.Footprint.RowPitch+480*8);
        auto half=[](uint16_t bits){const int exponent=(bits>>10)&31;const int fraction=bits&1023;if(exponent==31)return std::numeric_limits<double>::quiet_NaN();const double value=exponent?std::ldexp(1.0+fraction/1024.0,exponent-15):std::ldexp(fraction/1024.0,-14);return bits&0x8000?-value:value;};
        bool written=true;for(unsigned i=0;i<3;++i)written=written&&std::isfinite(half(pixel[i]))&&std::abs(half(pixel[i])-color[i])<0.03;
        D3D12_RANGE none{0,0};readback->Unmap(0,&none);
        results.push_back({mode.first,int(result),width,height,result==sl::Result::eOk&&written});
        if(dispatch.ReleaseAfterGpuDrain()!=sl::Result::eOk)throw std::runtime_error("DLSS resource cleanup failed");
    }
    return results;
}
