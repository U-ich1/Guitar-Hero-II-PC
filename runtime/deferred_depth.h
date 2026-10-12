#pragma once
#include "depth_conversion.h"
#include <rex/graphics/d3d12/deferred_command_list.h>
namespace gh2::temporal {
// Adapt the producer to the renderer's real serialized command stream.
class DeferredDepthCommands {
    rex::graphics::d3d12::DeferredCommandList& commands_;
public:
    explicit DeferredDepthCommands(rex::graphics::d3d12::DeferredCommandList& commands):commands_(commands){}
    void SetDescriptorHeaps(UINT count,ID3D12DescriptorHeap* const* heaps){if(count!=1)throw std::runtime_error("Depth descriptor heap count");commands_.SetDescriptorHeaps(heaps[0],nullptr);}
    void SetComputeRootSignature(ID3D12RootSignature* root){commands_.D3DSetComputeRootSignature(root);}
    void SetPipelineState(ID3D12PipelineState* state){commands_.D3DSetPipelineState(state);}
    void SetComputeRootDescriptorTable(UINT index,D3D12_GPU_DESCRIPTOR_HANDLE table){commands_.D3DSetComputeRootDescriptorTable(index,table);}
    void SetComputeRoot32BitConstants(UINT index,UINT count,const void* data,UINT offset){commands_.D3DSetComputeRoot32BitConstants(index,count,data,offset);}
    void SetComputeRootConstantBufferView(UINT index,D3D12_GPU_VIRTUAL_ADDRESS address){commands_.D3DSetComputeRootConstantBufferView(index,address);}
    void Dispatch(UINT x,UINT y,UINT z){commands_.D3DDispatch(x,y,z);}
};
}
