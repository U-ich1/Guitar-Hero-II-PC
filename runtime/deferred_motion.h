#pragma once
#include <rex/graphics/d3d12/deferred_command_list.h>
namespace gh2::temporal {
class DeferredMotionCommands {
    rex::graphics::d3d12::DeferredCommandList& list_;
public:
    explicit DeferredMotionCommands(rex::graphics::d3d12::DeferredCommandList& list):list_(list){}
    void SetDescriptorHeaps(UINT n,ID3D12DescriptorHeap*const* h){if(n!=1)throw std::runtime_error("Motion heap count");list_.SetDescriptorHeaps(h[0],nullptr);}
    void SetGraphicsRootSignature(ID3D12RootSignature* p){list_.D3DSetGraphicsRootSignature(p);}
    void SetPipelineState(ID3D12PipelineState* p){list_.D3DSetPipelineState(p);}
    void SetGraphicsRootDescriptorTable(UINT n,D3D12_GPU_DESCRIPTOR_HANDLE h){list_.D3DSetGraphicsRootDescriptorTable(n,h);}
    void SetGraphicsRoot32BitConstants(UINT n,UINT count,const void* data,UINT offset){list_.D3DSetGraphicsRoot32BitConstants(n,count,data,offset);}
    void RSSetViewports(UINT n,const D3D12_VIEWPORT* v){if(n!=1)throw std::runtime_error("Motion viewport count");list_.RSSetViewport(*v);}
    void RSSetScissorRects(UINT n,const D3D12_RECT* r){if(n!=1)throw std::runtime_error("Motion scissor count");list_.RSSetScissorRect(*r);}
    void OMSetRenderTargets(UINT n,const D3D12_CPU_DESCRIPTOR_HANDLE* h,BOOL single,const D3D12_CPU_DESCRIPTOR_HANDLE* d){list_.D3DOMSetRenderTargets(n,h,single,d);}
    void IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY p){list_.D3DIASetPrimitiveTopology(p);}
    void IASetVertexBuffers(UINT slot,UINT n,const D3D12_VERTEX_BUFFER_VIEW* b){list_.D3DIASetVertexBuffers(slot,n,b);}
    void DrawInstanced(UINT n,UINT instances,UINT first,UINT start){list_.D3DDrawInstanced(n,instances,first,start);}
};
}
