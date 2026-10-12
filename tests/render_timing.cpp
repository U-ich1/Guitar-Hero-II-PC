// Real D3D12 shaders and timestamp ring; no guest game code or visible window.
#include "../runtime/render_timing.h"
#include <rex/ui/d3d12/d3d12_provider.h>
#include <rex/ui/presenter.h>
#include <rex/cvar.h>
#include <iostream>
#include <stdexcept>
static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
    rex::InitLogging("build/render_timing_test.log",spdlog::level::debug);
    struct LoggingGuard{~LoggingGuard(){rex::ShutdownLogging();}} guard;
    require(rex::cvar::SetFlagByName("present_effect","bilinear")&&!rex::cvar::SetFlagByName("present_effect","fsr"),"Retired FSR still selectable");
    auto provider=rex::ui::d3d12::D3D12Provider::Create();require(bool(provider),"D3D12 device unavailable");
    auto presenter=provider->CreatePresenter();require(bool(presenter),"Bilinear presentation initialization failed");
    auto device=provider->GetDevice();auto queue=provider->GetDirectQueue();
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commands;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    require(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator))),"Allocator failed");
    require(SUCCEEDED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&commands))),"Commands failed");
    commands->Close();require(SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))),"Fence failed");
    const HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);require(bool(event),"Fence event failed");
    struct EventGuard{HANDLE value;~EventGuard(){CloseHandle(value);}} event_guard{event};
    gh2::profiling::GpuTimer timer("fixture_gpu");require(timer.Initialize(device,queue),"Timestamp initialization failed");
    for(uint64_t submission=1;submission<=66;++submission){
        allocator->Reset();commands->Reset(allocator.Get(),nullptr);
        // Deliberately withhold completed fence values to exhaust the ring.
        timer.Begin(device,queue,submission==66?65:0,commands.Get());
        if(submission==66)require(timer.completed_samples()==64,"Completed GPU timestamps not collected");
        timer.End(commands.Get(),submission);commands->Close();ID3D12CommandList* list=commands.Get();queue->ExecuteCommandLists(1,&list);
        require(SUCCEEDED(queue->Signal(fence.Get(),submission)),"Queue signal failed");
        // Only this fixture waits; the game's timer never waits for the GPU.
        require(SUCCEEDED(fence->SetEventOnCompletion(submission,event)),"Fence event failed");
        require(WaitForSingleObject(event,5000)==WAIT_OBJECT_0,"Synthetic submission timed out");
    }
    uint32_t sequence=0;unsigned sampled=0;gh2::profiling::Metric metric("fixture_sampled_draw");
    std::fenv_t before{},after{};std::fesetround(FE_DOWNWARD);std::fegetenv(&before);
    for(unsigned i=0;i<640;++i){gh2::profiling::SampledScope scope(metric,sequence);sampled+=scope.active();}
    std::fegetenv(&after);require(sampled==10&&std::memcmp(&before,&after,sizeof(before))==0,"Sample count or FP state changed");
    require(timer.skipped_samples()==1,"Busy ring did not skip measurement");
    std::cout<<"PASS: actual D3D12 bilinear presentation initialization, 64 completed timestamp pairs, busy-ring skip and reuse. No game launched.\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
