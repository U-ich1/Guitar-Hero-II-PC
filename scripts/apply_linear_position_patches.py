"""Connect the verified linear title position program to native snapshots."""
from pathlib import Path
root=Path('tools/rexglue-sdk')
def replace(relative,before,after):
    file=root/relative;source=file.read_text()
    if after in source:return
    if before=='{now.linear.index_bytes,before.linear.index_bytes,0,0}}};' and '{before.linear.index_max,uint32_t(gh2::temporal::SupportsSkinnedPositionShader(key.shader)),0,0}}};' in source:return
    if before=='void D3D12CommandProcessor::GH2RecordDepthSnapshot(unsigned layer) {' and 'void D3D12CommandProcessor::GH2GeneratePositionCandidates() {' in source:return
    if before=='      gh2_renderer_inputs_.history.Record(std::move(key),std::move(state));' and '// Preserve the exact bytes seen by this draw.' in source:return
    if before not in source:raise RuntimeError('Pinned linear position patch changed: '+relative+': '+before[:70])
    file.write_text(source.replace(before,after,1))
replace('include/rex/graphics/d3d12/command_processor.h','  void GH2RecordDepthSnapshot(unsigned layer);',
        '  void GH2RecordDepthSnapshot(unsigned layer);\n  void GH2GeneratePositionCandidates();')
p='src/graphics/d3d12/command_processor.cpp'
replace(p,'void D3D12CommandProcessor::GH2RecordDepthSnapshot(unsigned layer) {', '''void D3D12CommandProcessor::GH2GeneratePositionCandidates() {
  auto& input=gh2_renderer_inputs_;
  auto* current=input.vertices.Active();auto* previous=input.vertices.PreviousSlot();
  if(!current||!previous)return;
  unsigned candidates=0,recorded=0,skipped=0;
  try {
    input.history.ForEachPositionCandidate([&](const gh2::temporal::GeometryKey& key,const gh2::temporal::GeometryState& now,const gh2::temporal::GeometryState& before){
      if(key.shader!=gh2::temporal::kLinearPositionShader)return;++candidates;
      if(recorded>=16||now.linear.index_size!=before.linear.index_size||now.linear.vertex_endian!=before.linear.vertex_endian||now.linear.index_endian!=before.linear.index_endian){++skipped;return;}
      auto* device=GetD3D12Provider().GetDevice();
      if(!input.positions_initialized){input.linear_positions.Initialize(device);input.positions_initialized=true;}
      auto* slot=input.clip_pool.Acquire(device,submission_completed_,submission_current_,current->buffer.Get(),previous->buffer.Get(),key.index_count);
      if(!slot){++skipped;return;}
      gh2::temporal::LinearClipParameters parameters;gh2::temporal::SetLinearTransforms(parameters,now,before);
      parameters.geometry={{{now.linear.vertex_offset,before.linear.vertex_offset,now.linear.index_offset,before.linear.index_offset},
          {36,key.index_count,now.linear.index_size,now.linear.vertex_endian},
          {now.linear.index_endian,now.linear.vertex_bytes,before.linear.vertex_bytes,1},
          {now.linear.index_bytes,before.linear.index_bytes,0,0}}};
      D3D12_GPU_VIRTUAL_ADDRESS constants=0;
      auto* upload=constant_buffer_pool_->Request(submission_current_,512,256,nullptr,nullptr,&constants);
      if(!upload){++skipped;return;}std::memset(upload,0,512);std::memcpy(upload,&parameters,sizeof(parameters));
      PushTransitionBarrier(slot->buffer.Get(),slot->state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);slot->state=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;SubmitBarriers();
      gh2::temporal::DeferredDepthCommands commands(deferred_command_list_);
      if(input.linear_positions.Record(&commands,current->buffer.Get(),previous->buffer.Get(),slot->buffer.Get(),slot->heap.Get(),slot->heap->GetGPUDescriptorHandleForHeapStart(),constants,parameters,key.shader,true)) {
        ++recorded;
        current->submission=std::max(current->submission,submission_current_);previous->submission=std::max(previous->submission,submission_current_);
        const auto readable=D3D12_RESOURCE_STATES(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
        PushTransitionBarrier(slot->buffer.Get(),slot->state,readable);slot->state=readable;SubmitBarriers();
      }else ++skipped;
    });
  }catch(const std::exception& error){REXLOG_ERROR("GH2 temporal positions: generation failed: {}",error.what());}
  if(recorded) {
    deferred_command_list_.SetDescriptorHeaps(bindless_resources_used_?view_bindless_heap_:view_bindful_heap_current_,bindless_resources_used_?sampler_bindless_heap_current_:sampler_bindful_heap_current_);
    current_guest_pipeline_=nullptr;current_external_pipeline_=nullptr;current_graphics_root_up_to_date_=0;
  }
  REXLOG_INFO("GH2 temporal positions: shader=2eba09b0974055eb candidates={} recorded={} skipped={} (raw clips, not certified motion)",candidates,recorded,skipped);
}
void D3D12CommandProcessor::GH2RecordDepthSnapshot(unsigned layer) {''')
replace(p,'    gh2_renderer_inputs_.vertices.EndFrame();',
        '    GH2GeneratePositionCandidates();\n    gh2_renderer_inputs_.vertices.EndFrame();')
replace(p,'      gh2_renderer_inputs_.history.Record(std::move(key),std::move(state));', '''      // Preserve the exact bytes seen by this draw. Shared-range snapshots
      // alone are insufficient when guest scratch buffers change between draws.
      if(key.shader==gh2::temporal::kLinearPositionShader&&!memexport_used&&
         primitive_processing_result.index_buffer_type==PrimitiveProcessor::ProcessedIndexBufferType::kGuestDMA&&
         primitive_processing_result.host_primitive_type==xenos::PrimitiveType::kTriangleList&&
         !primitive_processing_result.IsTessellated()&&!primitive_processing_result.host_primitive_reset_enabled&&
         key.index_count<=gh2::temporal::ClipFramePool::kMaximumVertices&&
         regs.Get<reg::VGT_INDX_OFFSET>().indx_offset==0&&regs.Get<reg::VGT_MIN_VTX_INDX>().min_indx==0&&regs.Get<reg::VGT_MAX_VTX_INDX>().max_indx==xenos::kVertexIndexMask) {
        if(auto* slot=gh2_renderer_inputs_.vertices.Active()) {
          const auto fetch=regs.GetVertexFetch(95);
          const uint32_t index_size=primitive_processing_result.host_index_format==xenos::IndexFormat::kInt16?2:4;
          const uint32_t index_bytes=key.index_count*index_size;
          const uint32_t index_prefix=key.index_base&3;
          const uint32_t aligned_index_bytes=(index_prefix+index_bytes+3)&~uint32_t(3);
          const auto vertex_range=gh2_renderer_inputs_.vertices.ReserveDraw(fetch.address<<2,fetch.size<<2,submission_current_);
          const auto index_range=gh2_renderer_inputs_.vertices.ReserveDraw(key.index_base&~uint32_t(3),aligned_index_bytes,submission_current_);
          if(fetch.type==xenos::FetchConstantType::kVertex&&vertex_range&&index_range) {
            shared_memory_->UseAsCopySource();SubmitBarriers();
            deferred_command_list_.D3DCopyBufferRegion(slot->buffer.Get(),vertex_range->offset,shared_memory_->GetBuffer(),vertex_range->source,vertex_range->size);
            deferred_command_list_.D3DCopyBufferRegion(slot->buffer.Get(),index_range->offset,shared_memory_->GetBuffer(),index_range->source,index_range->size);
            shared_memory_->UseForReading();SubmitBarriers();
            state.linear={vertex_range->offset,vertex_range->size,index_range->offset+index_prefix,index_bytes,index_size,
                uint32_t(fetch.endian),uint32_t(primitive_processing_result.host_shader_index_endian),true};
          }
        }
      }
      gh2_renderer_inputs_.history.Record(std::move(key),std::move(state));''')
replace(p,'  gh2_renderer_inputs_.vertices.ClearAfterGpuDrain();',
        '  gh2_renderer_inputs_.vertices.ClearAfterGpuDrain();\n  gh2_renderer_inputs_.clip_pool.ClearAfterGpuDrain();')

replace(p,'{now.linear.index_bytes,before.linear.index_bytes,0,0}}};',
        '''{now.linear.index_bytes,before.linear.index_bytes,0,now.linear.base_vertex},
          {now.linear.index_min,now.linear.index_max,before.linear.base_vertex,before.linear.index_min},
          {before.linear.index_max,0,0,0}}};''')
replace(p,'key.index_count<=gh2::temporal::ClipFramePool::kMaximumVertices&&\n         regs.Get<reg::VGT_INDX_OFFSET>().indx_offset==0&&regs.Get<reg::VGT_MIN_VTX_INDX>().min_indx==0&&regs.Get<reg::VGT_MAX_VTX_INDX>().max_indx==xenos::kVertexIndexMask)',
        'key.index_count<=gh2::temporal::ClipFramePool::kMaximumVertices)')
replace(p,'uint32_t(fetch.endian),uint32_t(primitive_processing_result.host_shader_index_endian),true};',
        '''uint32_t(fetch.endian),uint32_t(primitive_processing_result.host_shader_index_endian),true};
            state.linear.base_vertex=regs.Get<reg::VGT_INDX_OFFSET>().indx_offset;
            state.linear.index_min=regs.Get<reg::VGT_MIN_VTX_INDX>().min_indx;state.linear.index_max=regs.Get<reg::VGT_MAX_VTX_INDX>().max_indx;''')

# Upgrade earlier copies so a partial success still restores state on failure.
file=root/p;text=file.read_text()
old='''    if(recorded) {
      deferred_command_list_.SetDescriptorHeaps(bindless_resources_used_?view_bindless_heap_:view_bindful_heap_current_,bindless_resources_used_?sampler_bindless_heap_current_:sampler_bindful_heap_current_);
      current_guest_pipeline_=nullptr;current_external_pipeline_=nullptr;current_graphics_root_up_to_date_=0;
    }
  }catch(const std::exception& error){REXLOG_ERROR("GH2 temporal positions: generation failed: {}",error.what());}'''
new='''  }catch(const std::exception& error){REXLOG_ERROR("GH2 temporal positions: generation failed: {}",error.what());}
  if(recorded) {
    deferred_command_list_.SetDescriptorHeaps(bindless_resources_used_?view_bindless_heap_:view_bindful_heap_current_,bindless_resources_used_?sampler_bindless_heap_current_:sampler_bindful_heap_current_);
    current_guest_pipeline_=nullptr;current_external_pipeline_=nullptr;current_graphics_root_up_to_date_=0;
  }'''
if old in text:file.write_text(text.replace(old,new,1))
