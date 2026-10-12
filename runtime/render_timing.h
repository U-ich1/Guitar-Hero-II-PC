#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cfenv>
#include <cstdint>
#include <rex/logging.h>
#include <rex/ui/d3d12/d3d12_api.h>
namespace gh2::profiling {
using Clock=std::chrono::steady_clock;
// Each metric is owned by a serialized renderer path, never by guest code.
class Metric {
    const char* name_; Clock::time_point start_=Clock::now();
    uint64_t count_=0; double total_=0,maximum_=0; unsigned over16_=0,over33_=0;
public:
    explicit Metric(const char* name):name_(name){}
    void Add(double milliseconds) {
        std::fenv_t env;std::feholdexcept(&env);
        ++count_;total_+=milliseconds;maximum_=std::max(maximum_,milliseconds);
        over16_+=milliseconds>16.667;over33_+=milliseconds>33.334;
        const auto now=Clock::now();const double seconds=std::chrono::duration<double>(now-start_).count();
        if(seconds>=5) {
            REXLOG_INFO("GH2 timing: {} samples={} avg_ms={:.3f} max_ms={:.3f} total_ms={:.3f} window_s={:.3f} over16={} over33={}",name_,count_,total_/double(count_),maximum_,total_,seconds,over16_,over33_);
            start_=now;count_=0;total_=maximum_=0;over16_=over33_=0;
        }
        std::fesetenv(&env);
    }
};
class Scope {
    Metric& metric_;Clock::time_point start_=Clock::now();
public:
    explicit Scope(Metric& metric):metric_(metric){}
    ~Scope(){std::fenv_t env;std::feholdexcept(&env);metric_.Add(std::chrono::duration<double,std::milli>(Clock::now()-start_).count());std::fesetenv(&env);}
};
// Draw translation runs tens of thousands of times per second. Take one
// measurement per 64 calls rather than paying two clocks and FP environment
// saves for every draw. Submitted GPU work and slow-path metrics stay complete.
class SampledScope {
    Metric& metric_;bool active_;Clock::time_point start_{};
public:
    SampledScope(Metric& metric,uint32_t& sequence):metric_(metric),active_((sequence++&63)==0) {
        if(active_)start_=Clock::now();
    }
    bool active()const{return active_;}
    ~SampledScope(){if(active_){std::fenv_t env;std::feholdexcept(&env);
        metric_.Add(std::chrono::duration<double,std::milli>(Clock::now()-start_).count());std::fesetenv(&env);}}
};
// Timestamp pairs are read only after the existing submission fence completes.
// A busy ring skips measurements rather than waiting or reusing in-flight queries.
class GpuTimer {
    static constexpr unsigned Slots=64;
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> heap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> readback_;
    std::array<uint64_t,Slots> fences_{}; uint64_t frequency_=0;
    uint64_t* values_=nullptr; unsigned next_=0; int active_=-1; bool attempted_=false;
    Metric metric_; uint64_t completed_samples_=0,skipped_samples_=0;
public:
    explicit GpuTimer(const char* name):metric_(name){}
    uint64_t completed_samples() const{return completed_samples_;}
    uint64_t skipped_samples() const{return skipped_samples_;}
    ~GpuTimer(){if(values_)readback_->Unmap(0,nullptr);}
    bool Initialize(ID3D12Device* device,ID3D12CommandQueue* queue) {
        if(attempted_)return bool(values_);attempted_=true;
        D3D12_QUERY_HEAP_DESC query{};query.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;query.Count=Slots*2;
        D3D12_HEAP_PROPERTIES properties{};properties.Type=D3D12_HEAP_TYPE_READBACK;properties.CreationNodeMask=properties.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC resource{};resource.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;resource.Width=Slots*2*sizeof(uint64_t);resource.Height=1;resource.DepthOrArraySize=resource.MipLevels=1;resource.SampleDesc.Count=1;resource.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if(FAILED(queue->GetTimestampFrequency(&frequency_))||!frequency_||FAILED(device->CreateQueryHeap(&query,IID_PPV_ARGS(&heap_)))||FAILED(device->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&resource,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback_)))) {
            REXLOG_WARN("GH2 GPU timestamp measurements unavailable; rendering continues.");return false;
        }
        D3D12_RANGE range{0,size_t(resource.Width)};
        if(FAILED(readback_->Map(0,&range,reinterpret_cast<void**>(&values_))))return false;
        return true;
    }
    void Begin(ID3D12Device* device,ID3D12CommandQueue* queue,uint64_t completed,ID3D12GraphicsCommandList* commands) {
        active_=-1;if(!Initialize(device,queue))return;
        std::fenv_t env;std::feholdexcept(&env);
        for(unsigned i=0;i<Slots;++i)if(fences_[i]&&fences_[i]<=completed){
            const uint64_t begin=values_[i*2],end=values_[i*2+1];
            if(end>=begin){metric_.Add(double(end-begin)*1000.0/double(frequency_));++completed_samples_;}fences_[i]=0;
        }
        std::fesetenv(&env);
        if(fences_[next_]){++skipped_samples_;return;}
        active_=int(next_);commands->EndQuery(heap_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,next_*2);
    }
    void End(ID3D12GraphicsCommandList* commands,uint64_t submission) {
        if(active_<0)return;const unsigned slot=unsigned(active_);
        commands->EndQuery(heap_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,slot*2+1);
        commands->ResolveQueryData(heap_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,slot*2,2,readback_.Get(),slot*2*sizeof(uint64_t));
        fences_[slot]=submission;next_=(slot+1)%Slots;active_=-1;
    }
};
}
