#pragma once
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

namespace gh2::temporal {
// Coordinates are explicit: allocation padding is never guessed to be image
// content. The caller must preserve the source before later layers overwrite
// it, or provide a matching layer mask before describing it as DLSS depth.
struct DepthRegion {uint32_t left,top,width,height;float scale,offset;uint32_t destination_left=0,destination_top=0;};
class DepthConversion {
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_;
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>,2> pipelines_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> clear_;
    static void Require(HRESULT result){if(FAILED(result))throw std::runtime_error("Depth conversion D3D12 operation failed");}
public:
    void Initialize(ID3D12Device* device) {
        D3D12_DESCRIPTOR_RANGE ranges[2]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,1}};
        D3D12_ROOT_PARAMETER parameters[2]{};parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameters[0].DescriptorTable={2,ranges};
        parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;parameters[1].Constants={0,0,10};
        D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=2;desc.pParameters=parameters;
        Microsoft::WRL::ComPtr<ID3DBlob> blob,error;
        Require(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error));
        Require(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root_)));
        const char* shader=R"(
#if MULTISAMPLED
Texture2DMS<float> source : register(t0);
#else
Texture2D<float> source : register(t0);
#endif
RWTexture2D<float> destination : register(u0);
cbuffer Region : register(b0) {uint2 origin;uint2 extent;uint samples;float depthScale;float depthOffset;uint unused;uint2 destinationOrigin;};
[numthreads(8,8,1)] void main(uint3 tid:SV_DispatchThreadID) {
    if(any(tid.xy>=extent))return;
    float nearest=1.0;
    for(uint s=0;s<samples;++s) {
#if MULTISAMPLED
        float value=source.Load(int2(origin+tid.xy),s);
#else
        float value=source.Load(int3(origin+tid.xy,0));
#endif
        // Map the observed layer's viewport depth range back into [0,1].
        // Pixels outside this range are invalid/background; do not clamp them
        // into a foreground layer. Min picks nearest canonical forward depth.
        float z=(value-depthOffset)/depthScale;
        if(isfinite(z)&&z>=0.0&&z<=1.0)nearest=min(nearest,z);
    }
    destination[destinationOrigin+tid.xy]=nearest;
}
[numthreads(8,8,1)] void clear_main(uint3 tid:SV_DispatchThreadID) {if(all(tid.xy<extent))destination[tid.xy]=1.0;}
)";
        for(unsigned i=0;i<2;++i){
            D3D_SHADER_MACRO macros[]={{"MULTISAMPLED",i?"1":"0"},{nullptr,nullptr}};
            blob.Reset();error.Reset();
            const auto result=D3DCompile(shader,std::strlen(shader),"gh2_depth_conversion",macros,nullptr,"main","cs_5_1",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&error);
            if(FAILED(result))throw std::runtime_error(error?std::string(static_cast<const char*>(error->GetBufferPointer()),error->GetBufferSize()):"Depth shader compilation failed");
            D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline{};pipeline.pRootSignature=root_.Get();pipeline.CS={blob->GetBufferPointer(),blob->GetBufferSize()};
            Require(device->CreateComputePipelineState(&pipeline,IID_PPV_ARGS(&pipelines_[i])));
        }
        D3D_SHADER_MACRO macros[]={{"MULTISAMPLED","0"},{nullptr,nullptr}};blob.Reset();error.Reset();
        Require(D3DCompile(shader,std::strlen(shader),"gh2_depth_clear",macros,nullptr,"clear_main","cs_5_1",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&error));
        D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline{};pipeline.pRootSignature=root_.Get();pipeline.CS={blob->GetBufferPointer(),blob->GetBufferSize()};
        Require(device->CreateComputePipelineState(&pipeline,IID_PPV_ARGS(&clear_)));
    }
    template<class Commands>
    bool Record(Commands* commands,ID3D12Resource* source,ID3D12Resource* output,
                ID3D12DescriptorHeap* descriptors,D3D12_GPU_DESCRIPTOR_HANDLE table,DepthRegion region) {
        if(!commands||!source||!output||!descriptors||source==output||!root_||
           !region.width||!region.height||!std::isfinite(region.scale)||!std::isfinite(region.offset)||std::abs(region.scale)<1e-9)return false;
        const auto s=source->GetDesc(),o=output->GetDesc();
        if(s.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||o.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||
           uint64_t(region.left)+region.width>s.Width||uint64_t(region.top)+region.height>s.Height||
           uint64_t(region.destination_left)+region.width>o.Width||uint64_t(region.destination_top)+region.height>o.Height||o.Format!=DXGI_FORMAT_R32_FLOAT||o.SampleDesc.Count!=1||
           !(o.Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)||!s.SampleDesc.Count)return false;
        // The caller owns descriptor lifetime, source SRV (including depth
        // format/plane), resource states, GPU lifetime and state restoration.
        // This function issues no waits, allocations, transitions or readbacks.
        struct Constants{uint32_t left,top,width,height,samples;float scale,offset;uint32_t padding,destination_left,destination_top;}constants{region.left,region.top,region.width,region.height,s.SampleDesc.Count,region.scale,region.offset,0,region.destination_left,region.destination_top};
        commands->SetDescriptorHeaps(1,&descriptors);commands->SetComputeRootSignature(root_.Get());
        commands->SetPipelineState(pipelines_[s.SampleDesc.Count>1?1:0].Get());
        commands->SetComputeRootDescriptorTable(0,table);commands->SetComputeRoot32BitConstants(1,10,&constants,0);
        commands->Dispatch((region.width+7)/8,(region.height+7)/8,1);
        return true;
    }
    template<class Commands>
    void Clear(Commands* commands,ID3D12Resource* output,ID3D12DescriptorHeap* heap,D3D12_GPU_DESCRIPTOR_HANDLE table) {
        const auto desc=output->GetDesc();
        std::array<uint32_t,10> constants{};constants[2]=uint32_t(desc.Width);constants[3]=desc.Height;
        commands->SetDescriptorHeaps(1,&heap);commands->SetComputeRootSignature(root_.Get());commands->SetPipelineState(clear_.Get());
        commands->SetComputeRootDescriptorTable(0,table);commands->SetComputeRoot32BitConstants(1,10,constants.data(),0);
        commands->Dispatch((uint32_t(desc.Width)+7)/8,(desc.Height+7)/8,1);
    }
};
}
