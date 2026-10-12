#pragma once
// Fixture-owned resources survive deferred execution and the completion fence.
// This joins the real position compute and raster paths without running GH2.
#include <rex/ui/linear_motion_clips.h>
#include <rex/ui/clip_frame_pool.h>
#include <rex/ui/deferred_depth.h>
struct MotionChain {
    gh2::temporal::LinearMotionClips producer;
    gh2::temporal::ClipFramePool pool;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,3> uploads;
    ID3D12Resource* Record(ID3D12Device* device,
            rex::graphics::d3d12::DeferredCommandList& deferred,
            const std::array<gh2::temporal::MotionVertex,3>& vertices,bool skinned=false) {
        producer.Initialize(device);
        gh2::temporal::LinearClipParameters parameters;
        for(unsigned i=0;i<3;++i)parameters.current_model[i][i]=parameters.previous_model[i][i]=1;
        for(unsigned i=0;i<4;++i)parameters.current_projection[i][i]=parameters.previous_projection[i][i]=1;
        parameters.geometry={{{0,0,0,0},{36,3,2,0},{0,108,108,0},{0,0,0,0},{0,0xffffff,0,0},{0xffffff,0,0,0}}};
        const unsigned stride=skinned?48:36;
        parameters.geometry[1][0]=stride;
        parameters.geometry[2][1]=parameters.geometry[2][2]=stride*3;
        parameters.geometry[5][1]=skinned;
        parameters.current_weight_constant[0]=parameters.previous_weight_constant[0]=1;
        for(unsigned bone=0;bone<4;++bone)for(unsigned row=0;row<3;++row) {
            parameters.current_bones[bone*3+row][row]=parameters.previous_bones[bone*3+row][row]=1;
            parameters.previous_bones[bone*3+row][3]=vertices[0].previous[row]-vertices[0].current[row];
        }
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width=1024;desc.Height=1;desc.DepthOrArraySize=1;desc.MipLevels=1;
        desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_UPLOAD;
        for(unsigned i=0;i<3;++i) {
            if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,
                    D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&uploads[i]))))
                throw std::runtime_error("Motion chain upload allocation failed");
            void* mapped=nullptr;D3D12_RANGE none{0,0};
            if(FAILED(uploads[i]->Map(0,&none,&mapped)))throw std::runtime_error("Motion chain map failed");
            std::memset(mapped,0,1024);
            if(i==2)std::memcpy(mapped,&parameters,sizeof(parameters));
            else for(unsigned v=0;v<3;++v)std::memcpy(static_cast<uint8_t*>(mapped)+v*stride,
                    i&&!skinned?vertices[v].previous.data():vertices[v].current.data(),12);
            if(skinned&&i<2)for(unsigned v=0;v<3;++v){const float weights[3]={.2f,.3f,.1f};std::memcpy(static_cast<uint8_t*>(mapped)+v*stride+12,weights,12);}
            uploads[i]->Unmap(0,nullptr);
        }
        auto* slot=pool.Acquire(device,0,1,uploads[0].Get(),uploads[1].Get(),3);
        if(!slot)throw std::runtime_error("Motion chain clip allocation failed");
        gh2::temporal::DeferredDepthCommands compute(deferred);
        if(!producer.Record(&compute,uploads[0].Get(),uploads[1].Get(),slot->buffer.Get(),
                slot->heap.Get(),slot->heap->GetGPUDescriptorHandleForHeapStart(),
                uploads[2]->GetGPUVirtualAddress(),parameters,skinned?0xb6d10de26caf4b00ULL:gh2::temporal::kLinearPositionShader,true))
            throw std::runtime_error("Motion chain compute rejected fixture");
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition={slot->buffer.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER};
        deferred.D3DResourceBarrier(1,&barrier);
        return slot->buffer.Get();
    }
};
