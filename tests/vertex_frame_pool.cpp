#include "../runtime/vertex_frame_pool.h"
#include <dxgi1_4.h>
#include <iostream>
#include <cstring>
using Microsoft::WRL::ComPtr;
static void Require(HRESULT hr){if(FAILED(hr))throw std::runtime_error("Vertex history GPU operation failed");}
int main(){try{
    ComPtr<IDXGIFactory1> factory;Require(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    ComPtr<IDXGIAdapter1> adapter;ComPtr<ID3D12Device> device;
    for(UINT i=0;factory->EnumAdapters1(i,&adapter)!=DXGI_ERROR_NOT_FOUND;++i){DXGI_ADAPTER_DESC1 desc{};adapter->GetDesc1(&desc);if(!(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)&&SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device))))break;adapter.Reset();}
    if(!device)throw std::runtime_error("No hardware GPU");
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};Require(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12Fence> fence;Require(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)throw std::runtime_error("Fence event unavailable");struct Guard{HANDLE event;~Guard(){CloseHandle(event);}}guard{event};
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=64;desc.Height=1;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_UPLOAD;ComPtr<ID3D12Resource> upload,readback;
    Require(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload)));
    heap.Type=D3D12_HEAP_TYPE_READBACK;Require(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)));
    gh2::temporal::VertexFramePool pool;uint64_t submission=0;
    auto verify=[&](gh2::temporal::VertexFramePool::Slot* slot,uint32_t expected){
        ComPtr<ID3D12CommandAllocator> alloc;ComPtr<ID3D12GraphicsCommandList> commands;Require(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&alloc)));Require(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc.Get(),nullptr,IID_PPV_ARGS(&commands)));
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition={slot->buffer.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,slot->state,D3D12_RESOURCE_STATE_COPY_SOURCE};commands->ResourceBarrier(1,&barrier);
        commands->CopyBufferRegion(readback.Get(),0,slot->buffer.Get(),0,64);
        std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);commands->ResourceBarrier(1,&barrier);Require(commands->Close());
        ID3D12CommandList* list=commands.Get();queue->ExecuteCommandLists(1,&list);Require(queue->Signal(fence.Get(),++submission));Require(fence->SetEventOnCompletion(submission,event));if(WaitForSingleObject(event,10000)!=WAIT_OBJECT_0)throw std::runtime_error("Vertex readback timeout");
        void* data=nullptr;D3D12_RANGE range{0,64};Require(readback->Map(0,&range,&data));for(unsigned i=0;i<16;++i){uint32_t v;std::memcpy(&v,static_cast<const uint8_t*>(data)+4*i,4);if(v!=expected+i)throw std::runtime_error("Previous vertex bytes overwritten");}D3D12_RANGE none{0,0};readback->Unmap(0,&none);
    };
    // Match the complete native probe duration, including repeated pool reuse.
    for(uint32_t frame=0;frame<16;++frame){
        auto* slot=pool.Begin(device.Get(),fence->GetCompletedValue());if(!slot)throw std::runtime_error("Frame slot unavailable");
        if(frame&&slot==pool.PreviousSlot())throw std::runtime_error("Previous frame reused prematurely");
        void* data=nullptr;D3D12_RANGE none{0,0};Require(upload->Map(0,&none,&data));for(unsigned i=0;i<16;++i){const uint32_t v=frame*100+i;std::memcpy(static_cast<uint8_t*>(data)+4*i,&v,4);}upload->Unmap(0,nullptr);
        bool needed=false;auto range=pool.Reserve(0,64,submission+1,needed);if(!range||!needed)throw std::runtime_error("Vertex range reservation failed");
        auto duplicate=pool.Reserve(0,64,submission+1,needed);if(!duplicate||needed)throw std::runtime_error("Range deduplication failed");
        auto first_draw=pool.ReserveDraw(0,64,submission+1),second_draw=pool.ReserveDraw(0,64,submission+1);
        if(!first_draw||!second_draw||first_draw->offset==second_draw->offset)throw std::runtime_error("Changing same-address draws shared a snapshot");
        if(pool.Reserve(512*1024*1024-4,64,submission+1,needed))throw std::runtime_error("Invalid physical range accepted");
        ComPtr<ID3D12CommandAllocator> alloc;ComPtr<ID3D12GraphicsCommandList> commands;Require(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&alloc)));Require(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc.Get(),nullptr,IID_PPV_ARGS(&commands)));
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        if(slot->state!=D3D12_RESOURCE_STATE_COPY_DEST) {
            barrier.Transition={slot->buffer.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,slot->state,D3D12_RESOURCE_STATE_COPY_DEST};
            commands->ResourceBarrier(1,&barrier);slot->state=D3D12_RESOURCE_STATE_COPY_DEST;
        }
        commands->CopyBufferRegion(slot->buffer.Get(),range->offset,upload.Get(),0,64);barrier.Transition={slot->buffer.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,slot->state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};commands->ResourceBarrier(1,&barrier);slot->state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;Require(commands->Close());
        ID3D12CommandList* list=commands.Get();queue->ExecuteCommandLists(1,&list);Require(queue->Signal(fence.Get(),++submission));Require(fence->SetEventOnCompletion(submission,event));if(WaitForSingleObject(event,10000)!=WAIT_OBJECT_0)throw std::runtime_error("Vertex copy timeout");
        verify(slot,frame*100);if(frame)verify(pool.PreviousSlot(),(frame-1)*100);pool.EndFrame();
    }
    pool.EndFrame(true);if(pool.Previous(0,64))throw std::runtime_error("History cut did not reset");pool.ClearAfterGpuDrain();
    gh2::temporal::VertexFramePool busy;
    for(unsigned i=0;i<4;++i){if(!busy.Begin(device.Get(),0))throw std::runtime_error("Busy pool setup");bool copy=false;if(!busy.Reserve(i*64,64,1,copy))throw std::runtime_error("Busy pool reservation");busy.EndFrame();}
    if(busy.Begin(device.Get(),0))throw std::runtime_error("In-flight geometry overwritten");
    if(!busy.Begin(device.Get(),1))throw std::runtime_error("Completed geometry slot not reused");
    bool needed=false;if(busy.Reserve(0,gh2::temporal::VertexFramePool::kCapacity+1,2,needed))throw std::runtime_error("Capacity overflow accepted");
    std::cout<<"PASS: actual GPU vertex snapshots preserve previous bytes, frame isolation, range deduplication, cuts, physical bounds, capacity and busy-fence skip/reuse. No game executed.\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
