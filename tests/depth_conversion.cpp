#ifdef GH2_DEFERRED_DEPTH_TEST
#include <rex/ui/depth_conversion.h>
#include <rex/ui/depth_frame_pool.h>
#else
#include "../runtime/depth_conversion.h"
#include "../runtime/depth_frame_pool.h"
#endif
#include <dxgi1_4.h>
#include <iostream>
#include <vector>
#ifdef GH2_DEFERRED_DEPTH_TEST
#include <rex/ui/deferred_depth.h>
#include <rex/graphics/d3d12/command_processor.h>
#include <rex/graphics/d3d12/graphics_system.h>
#include <rex/logging.h>
#endif
using Microsoft::WRL::ComPtr;
static void Require(HRESULT hr){if(FAILED(hr))throw std::runtime_error("Depth fixture D3D12 failure");}
int main(){try{
#ifdef GH2_DEFERRED_DEPTH_TEST
    rex::InitLogging("build/deferred_depth_test.log",spdlog::level::info);
    struct LoggingGuard{~LoggingGuard(){rex::ShutdownLogging();}}logging_guard;
    rex::graphics::d3d12::D3D12GraphicsSystem system;
    // No setup/guest execution: the concrete processor owns only its command
    // stream context here. No worker is started and memory is never accessed.
    rex::graphics::d3d12::D3D12CommandProcessor processor(&system,nullptr);
#endif
    ComPtr<IDXGIFactory1> factory;Require(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    ComPtr<IDXGIAdapter1> adapter;ComPtr<ID3D12Device> device;
    for(UINT i=0;factory->EnumAdapters1(i,&adapter)!=DXGI_ERROR_NOT_FOUND;++i){
        DXGI_ADAPTER_DESC1 desc{};adapter->GetDesc1(&desc);
        if(!(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)&&SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device))))break;
        adapter.Reset();
    }
    if(!device)throw std::runtime_error("No hardware D3D12 device");
    gh2::temporal::DepthConversion converter;converter.Initialize(device.Get());
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};Require(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12Fence> fence;Require(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)throw std::runtime_error("Fence event creation failed");
    struct Guard{HANDLE event;~Guard(){CloseHandle(event);}}guard{event};uint64_t submission=0;unsigned verified=0;
    struct Format{DXGI_FORMAT resource,dsv,srv;};
    const std::array<Format,2> formats={{{DXGI_FORMAT_R32G8X24_TYPELESS,DXGI_FORMAT_D32_FLOAT_S8X24_UINT,DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS},{DXGI_FORMAT_R24G8_TYPELESS,DXGI_FORMAT_D24_UNORM_S8_UINT,DXGI_FORMAT_R24_UNORM_X8_TYPELESS}}};
    for(auto format:formats)for(UINT samples:std::array<UINT,3>{1,2,4})for(float scale:std::array<float,3>{-0.9f,-0.1f,-0.02f}) {
        D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS quality{format.dsv,samples,D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE,0};
        Require(device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,&quality,sizeof(quality)));
        if(!quality.NumQualityLevels)throw std::runtime_error("Required hardware MSAA depth format unavailable");
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=128;desc.Height=96;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=samples;desc.Format=format.resource;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        ComPtr<ID3D12Resource> source,output;Require(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_DEPTH_WRITE,nullptr,IID_PPV_ARGS(&source)));
        desc.Width=72;desc.Height=24;desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_R32_FLOAT;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        Require(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&output)));
        ComPtr<ID3D12DescriptorHeap> ds,views;D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.NumDescriptors=1;hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        Require(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&ds)));
        hd.NumDescriptors=2;hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        Require(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&views)));
        D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};dsv.Format=format.dsv;dsv.ViewDimension=samples>1?D3D12_DSV_DIMENSION_TEXTURE2DMS:D3D12_DSV_DIMENSION_TEXTURE2D;
        device->CreateDepthStencilView(source.Get(),&dsv,ds->GetCPUDescriptorHandleForHeapStart());
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=format.srv;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.ViewDimension=samples>1?D3D12_SRV_DIMENSION_TEXTURE2DMS:D3D12_SRV_DIMENSION_TEXTURE2D;srv.Texture2D.MipLevels=1;
        auto cpu=views->GetCPUDescriptorHandleForHeapStart();device->CreateShaderResourceView(source.Get(),&srv,cpu);cpu.ptr+=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=DXGI_FORMAT_R32_FLOAT;uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;device->CreateUnorderedAccessView(output.Get(),nullptr,&uav,cpu);
        ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> commands;
        Require(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
        Require(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&commands)));
        const float offset=scale==-0.9f?0.9f:1.0f;
        // Padded background depth must not be mistaken for the active crop.
        commands->ClearDepthStencilView(ds->GetCPUDescriptorHandleForHeapStart(),D3D12_CLEAR_FLAG_DEPTH,0,0,0,nullptr);
        D3D12_RECT crop{5,7,37,31};
        commands->ClearDepthStencilView(ds->GetCPUDescriptorHandleForHeapStart(),D3D12_CLEAR_FLAG_DEPTH,offset+scale*0.5f,0,1,&crop);
        D3D12_RECT second_crop{37,7,69,31};
        commands->ClearDepthStencilView(ds->GetCPUDescriptorHandleForHeapStart(),D3D12_CLEAR_FLAG_DEPTH,offset+scale*0.25f,0,1,&second_crop);
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition={source.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};commands->ResourceBarrier(1,&barrier);
        auto gpu=views->GetGPUDescriptorHandleForHeapStart();gh2::temporal::DepthRegion region{5,7,32,24,scale,offset};
        if(converter.Record(commands.Get(),source.Get(),output.Get(),views.Get(),gpu,{127,0,32,24,scale,offset}))throw std::runtime_error("Invalid source crop accepted");
        if(converter.Record(commands.Get(),source.Get(),output.Get(),views.Get(),gpu,{5,7,32,24,0,offset}))throw std::runtime_error("Zero depth scale accepted");
        if(converter.Record(commands.Get(),source.Get(),output.Get(),views.Get(),gpu,{5,7,32,24,scale,offset,70,0}))throw std::runtime_error("Invalid atlas placement accepted");
        D3D12_RESOURCE_BARRIER ordering{};ordering.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;ordering.UAV.pResource=output.Get();
        gh2::temporal::DepthRegion second{37,7,32,24,scale,offset,32,0};
#ifdef GH2_DEFERRED_DEPTH_TEST
        rex::graphics::d3d12::DeferredCommandList deferred(processor);
        gh2::temporal::DeferredDepthCommands queued(deferred);
        converter.Clear(&queued,output.Get(),views.Get(),gpu);deferred.D3DResourceBarrier(1,&ordering);
        if(!converter.Record(&queued,source.Get(),output.Get(),views.Get(),gpu,region))throw std::runtime_error("Deferred depth conversion rejected");
        deferred.D3DResourceBarrier(1,&ordering);
        if(!converter.Record(&queued,source.Get(),output.Get(),views.Get(),gpu,second))throw std::runtime_error("Deferred second tile rejected");
        deferred.Execute(commands.Get(),nullptr);
#else
        converter.Clear(commands.Get(),output.Get(),views.Get(),gpu);commands->ResourceBarrier(1,&ordering);
        if(!converter.Record(commands.Get(),source.Get(),output.Get(),views.Get(),gpu,region))throw std::runtime_error("Depth conversion recording rejected");
        commands->ResourceBarrier(1,&ordering);
        if(!converter.Record(commands.Get(),source.Get(),output.Get(),views.Get(),gpu,second))throw std::runtime_error("Second depth tile rejected");
#endif
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT rows=0;UINT64 row_bytes=0,total=0;device->GetCopyableFootprints(&desc,0,1,0,&footprint,&rows,&row_bytes,&total);
        D3D12_RESOURCE_DESC rb{};rb.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rb.Width=total;rb.Height=1;rb.DepthOrArraySize=1;rb.MipLevels=1;rb.SampleDesc.Count=1;rb.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;heap.Type=D3D12_HEAP_TYPE_READBACK;ComPtr<ID3D12Resource> readback;
        Require(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&rb,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)));
        barrier.Transition={output.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};commands->ResourceBarrier(1,&barrier);
        D3D12_TEXTURE_COPY_LOCATION dest{};dest.pResource=readback.Get();dest.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dest.PlacedFootprint=footprint;
        D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=output.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;commands->CopyTextureRegion(&dest,0,0,0,&src,nullptr);
        Require(commands->Close());ID3D12CommandList* list=commands.Get();queue->ExecuteCommandLists(1,&list);Require(queue->Signal(fence.Get(),++submission));Require(fence->SetEventOnCompletion(submission,event));
        if(WaitForSingleObject(event,10000)!=WAIT_OBJECT_0)throw std::runtime_error("Depth GPU fixture timeout");
        void* bytes=nullptr;D3D12_RANGE range{0,size_t(total)};Require(readback->Map(0,&range,&bytes));
        for(unsigned y=0;y<24;++y)for(unsigned x=0;x<72;++x){float z;std::memcpy(&z,static_cast<const uint8_t*>(bytes)+footprint.Offset+y*footprint.Footprint.RowPitch+x*4,4);const float expected=x<32?0.5f:x<64?0.25f:1.0f;if(!std::isfinite(z)||std::abs(z-expected)>0.0001f)throw std::runtime_error("Assembled depth atlas pixel mismatch");}
        D3D12_RANGE none{0,0};readback->Unmap(0,&none);++verified;
        // Artificially withhold completed submissions to verify slot pressure
        // never replaces an in-flight output or blocks on a GPU fence.
        gh2::temporal::DepthFramePool pool;
        auto source_handle=views->GetCPUDescriptorHandleForHeapStart();
        for(unsigned i=0;i<12;++i)if(!pool.Acquire(device.Get(),0,1,32,24,source_handle))throw std::runtime_error("Depth slot allocation failed");
        if(pool.Acquire(device.Get(),0,1,32,24,source_handle))throw std::runtime_error("In-flight depth slot overwritten");
        if(!pool.Acquire(device.Get(),1,2,32,24,source_handle))throw std::runtime_error("Completed depth slot not reused");
        pool.ClearAfterGpuDrain();
        gh2::temporal::DepthFramePool shared_pool;
        auto* master=shared_pool.Acquire(device.Get(),0,1,32,24,source_handle);
        auto* tile=shared_pool.Acquire(device.Get(),0,2,32,24,source_handle,master->output.Get());
        if(!tile||tile==master||tile->output.Get()!=master->output.Get())throw std::runtime_error("Shared atlas descriptor isolation failed");
        for(unsigned i=0;i<10;++i)if(!shared_pool.Acquire(device.Get(),0,2,32,24,source_handle))throw std::runtime_error("Shared atlas pool setup");
        if(shared_pool.Acquire(device.Get(),1,3,32,24,source_handle))throw std::runtime_error("Shared atlas reused before all tile submissions completed");
        auto* continuation=shared_pool.Acquire(device.Get(),1,3,32,24,source_handle,tile->output.Get());
        if(!continuation||continuation->output.Get()!=tile->output.Get())throw std::runtime_error("Completed descriptor lease could not append a tile to its live atlas");
        auto* unrelated=shared_pool.Acquire(device.Get(),2,4,32,24,source_handle);
        if(unrelated&&unrelated->output.Get()==tile->output.Get())throw std::runtime_error("Continued atlas was reset before its final submission completed");
        if(!shared_pool.Acquire(device.Get(),3,4,32,24,source_handle))throw std::runtime_error("Shared atlas not reusable after all tiles completed");
    }
    std::cout<<"PASS: "<<verified<<" hardware GPU depth conversions, float24-host and UNORM depth, 1/2/4 samples, all three layer ranges, two-tile atlas placement, cleared missing coverage, crop/placement guards and shared-atlas fence lifetime.\n";
#ifdef GH2_DEFERRED_DEPTH_TEST
    std::cout<<"PASS: actual SDK deferred command execution and twelve-slot busy skip/completed reuse; no guest game or worker started.\n";
#endif
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
