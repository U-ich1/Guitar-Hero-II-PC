#ifdef GH2_DEFERRED_MOTION_TEST
#include <rex/ui/motion_raster.h>
#include <rex/ui/motion_frame_pool.h>
#include <rex/ui/deferred_motion.h>
#include <rex/graphics/d3d12/command_processor.h>
#include <rex/graphics/d3d12/graphics_system.h>
#include <rex/logging.h>
#else
#include "../runtime/motion_raster.h"
#include "../runtime/motion_frame_pool.h"
#endif
#include <dxgi1_4.h>
#include <iostream>
#include <cmath>
#include <vector>
#ifdef GH2_MOTION_CHAIN_TEST
#include "motion_chain.h"
#endif
using Microsoft::WRL::ComPtr;
static void Require(HRESULT hr){if(FAILED(hr))throw std::runtime_error("Motion fixture D3D12 failure");}
static double Half(uint16_t bits){const int e=(bits>>10)&31,f=bits&1023;if(e==31)return NAN;const double v=e?std::ldexp(1.0+f/1024.0,e-15):std::ldexp(f/1024.0,-14);return bits&0x8000?-v:v;}
int main(){try{
#ifdef GH2_DEFERRED_MOTION_TEST
    rex::InitLogging("build/deferred_motion_test.log",spdlog::level::info);
    struct LoggingGuard{~LoggingGuard(){rex::ShutdownLogging();}}logging_guard;
    rex::graphics::d3d12::D3D12GraphicsSystem system;
    rex::graphics::d3d12::D3D12CommandProcessor processor(&system,nullptr);
#endif
    ComPtr<IDXGIFactory1> factory;Require(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));ComPtr<IDXGIAdapter1> adapter;ComPtr<ID3D12Device> device;
    for(UINT i=0;factory->EnumAdapters1(i,&adapter)!=DXGI_ERROR_NOT_FOUND;++i){DXGI_ADAPTER_DESC1 desc{};adapter->GetDesc1(&desc);if(!(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)&&SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device))))break;adapter.Reset();}
    if(!device)throw std::runtime_error("No hardware GPU");gh2::temporal::MotionRaster raster;raster.Initialize(device.Get());
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};Require(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12Fence> fence;Require(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)throw std::runtime_error("Fence event failed");struct Guard{HANDLE event;~Guard(){CloseHandle(event);}}guard{event};uint64_t submission=0;
    unsigned checked=0;
#ifdef GH2_MOTION_CHAIN_TEST
    constexpr unsigned scenarios=12;
#else
    constexpr unsigned scenarios=9;
#endif
    for(unsigned scenario=0;scenario<scenarios;++scenario){
        const unsigned mode=scenario%9;
        std::array<ComPtr<ID3D12Resource>,3> textures;
        const std::array<DXGI_FORMAT,3> formats={DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R16G16_FLOAT,DXGI_FORMAT_R8_UINT};
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=64;desc.Height=64;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        for(unsigned i=0;i<3;++i){desc.Format=formats[i];Require(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&textures[i])));}
        ComPtr<ID3D12DescriptorHeap> rtvs,srvs;D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=3;Require(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtvs)));
        hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=1;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;Require(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&srvs)));
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE,3> handles;auto handle=rtvs->GetCPUDescriptorHandleForHeapStart();const UINT stride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        for(unsigned i=0;i<3;++i){handles[i]=handle;device->CreateRenderTargetView(textures[i].Get(),nullptr,handle);handle.ptr+=stride;}
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=DXGI_FORMAT_R32_FLOAT;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Texture2D.MipLevels=1;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;device->CreateShaderResourceView(textures[0].Get(),&srv,srvs->GetCPUDescriptorHandleForHeapStart());
        std::array<gh2::temporal::MotionVertex,3> vertices={{{{-1,-1,.5f,1},{-1,-1,.5f,1},1},{{-1,3,.5f,1},{-1,3,.5f,1},1},{{3,-1,.5f,1},{3,-1,.5f,1},1}}};
        double expected_x=0,expected_y=0;uint8_t expected_valid=1;
        if(mode==1){for(auto& v:vertices)v.previous[0]+=.25f;expected_x=8;}
        if(mode==2){for(auto& v:vertices)v.previous[1]+=.25f;expected_y=-8;}
        if(mode==3){for(auto& v:vertices)for(auto& f:v.previous)f*=2;}
        if(mode==4){for(auto& v:vertices)v.previous[3]=-1;expected_valid=0;}
        if(mode==5){vertices[0].history_valid=0;expected_valid=0;}
        if(mode==6){for(auto& v:vertices)v.current[2]=.75f;expected_x=11;expected_y=12;}
        if(mode==7)expected_valid=0;
        if(mode==8){
            for(unsigned i=0;i<3;++i)for(auto& f:vertices[i].previous)f*=float(1u<<i);
            const double a=.5,b=31.5/128.0,c=32.5/128.0,w=a+b*2+c*4;
            const double x=(-a-2*b+12*c)/w,y=(-a+6*b-4*c)/w;
            expected_x=(x*.5+.5)*64-32.5;expected_y=(-y*.5+.5)*64-32.5;
        }
        D3D12_RESOURCE_DESC vb{};vb.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;vb.Width=sizeof(vertices);vb.Height=1;vb.DepthOrArraySize=1;vb.MipLevels=1;vb.SampleDesc.Count=1;vb.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;heap.Type=D3D12_HEAP_TYPE_UPLOAD;ComPtr<ID3D12Resource> vertex_buffer;Require(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&vb,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&vertex_buffer)));
        void* mapped=nullptr;D3D12_RANGE none{0,0};Require(vertex_buffer->Map(0,&none,&mapped));std::memcpy(mapped,vertices.data(),sizeof(vertices));vertex_buffer->Unmap(0,nullptr);
        ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> commands;Require(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));Require(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&commands)));
        const float depth_clear[]={.5f,0,0,0},zero[]={0,0,0,0},foreground[]={11,12,0,0},valid[]={1,0,0,0};
        commands->ClearRenderTargetView(handles[0],depth_clear,0,nullptr);commands->ClearRenderTargetView(handles[1],mode==6?foreground:zero,0,nullptr);commands->ClearRenderTargetView(handles[2],mode==6?valid:zero,0,nullptr);
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition={textures[0].Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};commands->ResourceBarrier(1,&barrier);
        const std::array<D3D12_CPU_DESCRIPTOR_HANDLE,2> targets={handles[1],handles[2]};auto gpu=srvs->GetGPUDescriptorHandleForHeapStart();
        if(raster.Record(commands.Get(),vertex_buffer.Get(),3,srvs.Get(),gpu,targets,64,64,{0,0,64,64},false,true))throw std::runtime_error("Unverified clip history accepted");
        if(raster.Record(commands.Get(),vertex_buffer.Get(),3,srvs.Get(),gpu,targets,64,64,{0,0,64,64},true,false))throw std::runtime_error("Unverified scene depth accepted");
        if(raster.Record(commands.Get(),vertex_buffer.Get(),6,srvs.Get(),gpu,targets,64,64,{0,0,64,64},true,true))throw std::runtime_error("Oversized vertex payload accepted");
        const D3D12_RECT scissor=mode==7?D3D12_RECT{0,0,32,64}:D3D12_RECT{0,0,64,64};
#ifdef GH2_DEFERRED_MOTION_TEST
        rex::graphics::d3d12::DeferredCommandList deferred(processor);
        gh2::temporal::DeferredMotionCommands queued(deferred);
        auto* raster_input=vertex_buffer.Get();
#ifdef GH2_MOTION_CHAIN_TEST
        MotionChain chain;
        if(mode<3)raster_input=chain.Record(device.Get(),deferred,vertices,scenario>=9);
#endif
        if(!raster.Record(&queued,raster_input,3,srvs.Get(),gpu,targets,64,64,scissor,true,true))throw std::runtime_error("Deferred motion refused fixture");
        deferred.Execute(commands.Get(),nullptr);
#else
        if(!raster.Record(commands.Get(),vertex_buffer.Get(),3,srvs.Get(),gpu,targets,64,64,scissor,true,true))throw std::runtime_error("Motion raster refused valid fixture");
#endif
        std::array<ComPtr<ID3D12Resource>,2> readback;std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT,2> footprints;
        for(unsigned i=0;i<2;++i){const auto out=textures[i+1]->GetDesc();UINT rows=0;UINT64 rowbytes=0,total=0;device->GetCopyableFootprints(&out,0,1,0,&footprints[i],&rows,&rowbytes,&total);
            D3D12_RESOURCE_DESC rb{};rb.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rb.Width=total;rb.Height=1;rb.DepthOrArraySize=1;rb.MipLevels=1;rb.SampleDesc.Count=1;rb.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;heap.Type=D3D12_HEAP_TYPE_READBACK;Require(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&rb,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback[i])));
            barrier.Transition={textures[i+1].Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE};commands->ResourceBarrier(1,&barrier);
            D3D12_TEXTURE_COPY_LOCATION dest{};dest.pResource=readback[i].Get();dest.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dest.PlacedFootprint=footprints[i];D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=textures[i+1].Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;commands->CopyTextureRegion(&dest,0,0,0,&src,nullptr);
        }
        Require(commands->Close());ID3D12CommandList* list=commands.Get();queue->ExecuteCommandLists(1,&list);Require(queue->Signal(fence.Get(),++submission));Require(fence->SetEventOnCompletion(submission,event));if(WaitForSingleObject(event,10000)!=WAIT_OBJECT_0)throw std::runtime_error("Motion GPU timeout");
        std::array<double,2> actual{};uint8_t mask=0;
        for(unsigned i=0;i<2;++i){D3D12_RANGE all{0,SIZE_T(readback[i]->GetDesc().Width)};Require(readback[i]->Map(0,&all,&mapped));const auto* pixel=static_cast<const uint8_t*>(mapped)+footprints[i].Offset+32*footprints[i].Footprint.RowPitch+32*(i?1:4);
#ifdef GH2_MOTION_CHAIN_TEST
            if(mode<3)for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
                const auto* sample=static_cast<const uint8_t*>(mapped)+footprints[i].Offset+y*footprints[i].Footprint.RowPitch+x*(i?1:4);
                if(i) {if(*sample!=1)throw std::runtime_error("Compute-to-raster chain left a coverage hole");}
                else {uint16_t h[2];std::memcpy(h,sample,4);const double vx=Half(h[0]),vy=Half(h[1]);
                    if(!std::isfinite(vx)||!std::isfinite(vy)||std::abs(vx-expected_x)>.03||std::abs(vy-expected_y)>.03)
                        throw std::runtime_error("Compute-to-raster full-image motion mismatch");}
            }
#endif
            if(i)mask=*pixel;else{uint16_t h[2];std::memcpy(h,pixel,4);actual={Half(h[0]),Half(h[1])};}readback[i]->Unmap(0,&none);}
        if(mask!=expected_valid||std::abs(actual[0]-expected_x)>.03||std::abs(actual[1]-expected_y)>.03)throw std::runtime_error("Motion/validity mismatch in scenario "+std::to_string(scenario));
        ++checked;
        gh2::temporal::MotionFramePool pool;
        for(unsigned i=0;i<6;++i){auto* slot=pool.Acquire(device.Get(),0,1,64,64,textures[0].Get());if(!slot||slot->certified)throw std::runtime_error("Motion slot allocation/certification error");}
        if(pool.Acquire(device.Get(),0,1,64,64,textures[0].Get()))throw std::runtime_error("In-flight motion slot overwritten");
        if(!pool.Acquire(device.Get(),1,2,64,64,textures[0].Get()))throw std::runtime_error("Completed motion slot not reusable");
        if(pool.Acquire(device.Get(),1,2,65,64,textures[0].Get()))throw std::runtime_error("Undersized scene depth accepted");
    }
    std::cout<<"PASS: "<<checked<<" hardware rasterized motion cases: stationary, horizontal/vertical backward pixels, homogeneous/perspective projection, invalid history, occlusion preservation, tile scissor and provenance guards. No game executed.\n";
#ifdef GH2_DEFERRED_MOTION_TEST
    std::cout<<"PASS: native deferred raster execution, fence-protected motion slots and uncertified output status.\n";
#endif
#ifdef GH2_MOTION_CHAIN_TEST
    std::cout<<"PASS: stationary and XY motion generated from raw current/previous vertex bytes through compute, UAV-to-vertex transition and raster in one native deferred stream.\n";
#endif
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
