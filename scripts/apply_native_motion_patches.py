"""Connect mapped title clips to an explicitly uncertified diagnostic field."""
from pathlib import Path
root=Path('tools/rexglue-sdk')
for name in ('host_clip_mapping.h','temporal_geometry.h','motion_raster.h','motion_frame_pool.h','deferred_motion.h','geometry_history.h','linear_motion_clips.h','temporal_renderer.h'):
    target=root/'include/rex/ui'/name;data=Path('runtime',name).read_bytes()
    if not target.exists()or target.read_bytes()!=data:target.write_bytes(data)
p=root/'src/graphics/d3d12/command_processor.cpp';text=p.read_text()
def replace(before,after):
    global text
    if after in text:return
    if before=='  unsigned candidates=0,recorded=0,skipped=0;' and 'std::array<gh2::temporal::MotionFramePool::Slot*,4> motion_slots{};' in text:return
    if before=='        PushTransitionBarrier(slot->buffer.Get(),slot->state,readable);slot->state=readable;SubmitBarriers();' and 'auto* atlas=input.depth_atlas[key.layer];' in text:return
    if before.startswith('  REXLOG_INFO("GH2 temporal positions:') and 'mapping_rejected={} (full-image clips, not certified motion)' in text:return
    if before=='  if(recorded) {\n    deferred_command_list_.SetDescriptorHeaps' and 'for(unsigned layer=1;layer<=2;++layer)if(motion_slots[layer])' in text:return
    if before not in text:raise RuntimeError('Pinned native motion patch changed: '+before[:70])
    text=text.replace(before,after,1)
replace('  unsigned candidates=0,recorded=0,skipped=0;','''  unsigned candidates=0,recorded=0,skipped=0,motion_draws=0,mapping_rejected=0;
  std::array<unsigned,4> layer_recorded{};
  std::array<gh2::temporal::MotionFramePool::Slot*,4> motion_slots{};''')
replace('if(recorded>=16||now.linear.index_size!=before.linear.index_size',
        'if(key.layer<1||key.layer>2||layer_recorded[key.layer]>=8||now.linear.index_size!=before.linear.index_size')
replace('      gh2::temporal::LinearClipParameters parameters;gh2::temporal::SetLinearTransforms(parameters,now,before);',
'''      gh2::temporal::LinearClipParameters parameters;
      if(!gh2::temporal::SetMappedLinearTransforms(parameters,now,before)){++mapping_rejected;++skipped;return;}''')
replace('        ++recorded;','        ++recorded;++layer_recorded[key.layer];')
replace('        PushTransitionBarrier(slot->buffer.Get(),slot->state,readable);slot->state=readable;SubmitBarriers();',
'''        PushTransitionBarrier(slot->buffer.Get(),slot->state,readable);slot->state=readable;SubmitBarriers();
        auto* atlas=input.depth_atlas[key.layer];
        if(atlas&&input.depth_recorded[key.layer]) {
          if(!input.motion_initialized){input.motion_raster.Initialize(device);input.motion_initialized=true;}
          auto*& motion=motion_slots[key.layer];
          if(!motion) {
            motion=input.motion_pool.Acquire(device,submission_completed_,submission_current_,input.width,input.height,atlas->output.Get());
            if(motion) {
              PushTransitionBarrier(atlas->output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);SubmitBarriers();
              const auto handles=motion->Handles(device);const float zero[4]={};
              deferred_command_list_.D3DClearRenderTargetView(handles[0],zero,0,nullptr);deferred_command_list_.D3DClearRenderTargetView(handles[1],zero,0,nullptr);
            }
          }
          if(motion) {
            gh2::temporal::DeferredMotionCommands raster_commands(deferred_command_list_);
            // Diagnostic only: unique matched buffers and the exact position
            // program establish candidate provenance, not certified object ID.
            if(input.motion_raster.RecordUncertifiedDiagnostic(&raster_commands,slot->buffer.Get(),key.index_count,
                motion->depth_heap.Get(),motion->depth_heap->GetGPUDescriptorHandleForHeapStart(),motion->Handles(device),
                input.width,input.height,{0,0,LONG(input.width),LONG(input.height)})) {
              ++motion_draws;atlas->submission=std::max(atlas->submission,submission_current_);
              slot->submission=std::max(slot->submission,submission_current_);motion->submission=submission_current_;
            }
          }
        }''')
replace('  if(recorded) {\n    deferred_command_list_.SetDescriptorHeaps', '''  for(unsigned layer=1;layer<=2;++layer)if(motion_slots[layer]) {
    auto* atlas=input.depth_atlas[layer];
    PushTransitionBarrier(atlas->output.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  }
  SubmitBarriers();
  if(recorded) {
    deferred_command_list_.SetDescriptorHeaps''')
replace('  REXLOG_INFO("GH2 temporal positions: shader=2eba09b0974055eb candidates={} recorded={} skipped={} (raw clips, not certified motion)",candidates,recorded,skipped);',
'''  if(motion_draws) {
    current_graphics_root_signature_=nullptr;current_graphics_root_up_to_date_=0;
    primitive_topology_=D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ff_viewport_update_needed_=true;ff_scissor_update_needed_=true;
    render_target_cache_->InvalidateCommandListRenderTargets();
  }
  REXLOG_INFO("GH2 temporal positions: shader=2eba09b0974055eb candidates={} recorded={} skipped={} mapping_rejected={} (full-image clips, not certified motion)",candidates,recorded,skipped,mapping_rejected);
  REXLOG_INFO("GH2 temporal motion: draws={} stage={} highway={} certified=false (partial shader coverage)",motion_draws,layer_recorded[1],layer_recorded[2]);''')
replace('      gh2::temporal::GeometryState state;', '''      gh2::temporal::GeometryState state;
      auto& mapping=state.clip_mapping;
      for(unsigned i=0;i<3;++i){mapping.ndc_scale[i]=viewport_info.ndc_scale[i];mapping.ndc_offset[i]=viewport_info.ndc_offset[i];}
      mapping.viewport={double(viewport_info.xy_extent[0])*0.5,double(viewport_info.xy_offset[0])+double(viewport_info.xy_extent[0])*0.5,
          -double(viewport_info.xy_extent[1])*0.5,double(viewport_info.xy_offset[1])+double(viewport_info.xy_extent[1])*0.5,
          viewport_info.xy_extent[0],viewport_info.xy_extent[1]};
      mapping.tile_x=-double(gh2_renderer_inputs_.window_x)*draw_resolution_scale_x;
      mapping.tile_y=-double(gh2_renderer_inputs_.window_y)*draw_resolution_scale_y;
      mapping.image_width=gh2_renderer_inputs_.width;mapping.image_height=gh2_renderer_inputs_.height;
      const auto format=regs.Get<reg::PA_CL_VTE_CNTL>();
      mapping.w_not_reciprocal=format.vtx_w0_fmt;mapping.xy_divided=format.vtx_xy_fmt;mapping.z_divided=format.vtx_z_fmt;
      mapping.canonical_depth=regs.Get<reg::PA_CL_CLIP_CNTL>().dx_clip_space_def;''')
replace('  gh2_renderer_inputs_.clip_pool.ClearAfterGpuDrain();',
        '  gh2_renderer_inputs_.clip_pool.ClearAfterGpuDrain();\n  gh2_renderer_inputs_.motion_pool.ClearAfterGpuDrain();')
p.write_text(text)

# Keep allocation and shader preparation after convention checks, and restore
# graphics caches even if candidate recording throws after changing state.
text=p.read_text()
if 'bool graphics_touched=false;' not in text:
    text=text.replace('  std::array<unsigned,4> layer_recorded{};', '  bool graphics_touched=false;\n  std::array<unsigned,4> layer_recorded{};',1)
    text=text.replace('            // Diagnostic only:', '            graphics_touched=true;\n            // Diagnostic only:',1)
    text=text.replace('  if(motion_draws) {','  if(graphics_touched) {',1)
old='''      auto* device=GetD3D12Provider().GetDevice();
      if(!input.positions_initialized){input.linear_positions.Initialize(device);input.positions_initialized=true;}
      auto* slot=input.clip_pool.Acquire(device,submission_completed_,submission_current_,current->buffer.Get(),previous->buffer.Get(),key.index_count);
      if(!slot){++skipped;return;}
      gh2::temporal::LinearClipParameters parameters;
      if(!gh2::temporal::SetMappedLinearTransforms(parameters,now,before)){++mapping_rejected;++skipped;return;}'''
new='''      gh2::temporal::LinearClipParameters parameters;
      if(!gh2::temporal::SetMappedLinearTransforms(parameters,now,before)){++mapping_rejected;++skipped;return;}
      auto* device=GetD3D12Provider().GetDevice();
      if(!input.positions_initialized){input.linear_positions.Initialize(device);input.positions_initialized=true;}
      auto* slot=input.clip_pool.Acquire(device,submission_completed_,submission_current_,current->buffer.Get(),previous->buffer.Get(),key.index_count);
      if(!slot){++skipped;return;}'''
if old in text:text=text.replace(old,new,1)
old='  if(!current||!previous)return;'
new='''  if(!current||!previous) {
    REXLOG_INFO("GH2 temporal positions: history_pending current={} previous={}",bool(current),bool(previous));
    return;
  }'''
if old in text:text=text.replace(old,new,1)
p.write_text(text)

# Position-equivalent title programs validated against their actual microcode.
text=p.read_text()
text=text.replace('key.shader!=gh2::temporal::kLinearPositionShader',
                  '!gh2::temporal::SupportsLinearPositionShader(key.shader)')
text=text.replace('key.shader==gh2::temporal::kLinearPositionShader',
                  'gh2::temporal::SupportsLinearPositionShader(key.shader)')
text=text.replace('shader=2eba09b0974055eb candidates={}',
                  'shader_family=linear3 candidates={}')
p.write_text(text)

# Four-bone title programs use 48-byte vertices and separate prior palettes.
text=p.read_text()
text=text.replace('gh2::temporal::SupportsLinearPositionShader(key.shader)', 'gh2::temporal::SupportsPositionShader(key.shader)')
text=text.replace('{36,key.index_count,now.linear.index_size,now.linear.vertex_endian}', '{gh2::temporal::PositionVertexStride(key.shader),key.index_count,now.linear.index_size,now.linear.vertex_endian}')
text=text.replace('{before.linear.index_max,0,0,0}}};', '{before.linear.index_max,uint32_t(gh2::temporal::SupportsSkinnedPositionShader(key.shader)),0,0}}};')
text=text.replace('Request(submission_current_,512,256,nullptr,nullptr,&constants)', 'Request(submission_current_,gh2::temporal::kPositionConstantBytes,256,nullptr,nullptr,&constants)')
text=text.replace('std::memset(upload,0,512);std::memcpy(upload,&parameters,sizeof(parameters));', 'std::memset(upload,0,gh2::temporal::kPositionConstantBytes);std::memcpy(upload,&parameters,sizeof(parameters));')
text=text.replace('shader_family=linear3 candidates={}', 'shader_family=linear3_skin5 candidates={}')
p.write_text(text)

text=p.read_text().replace('shader_family=linear3_skin3 candidates={}', 'shader_family=linear3_skin5 candidates={}')
p.write_text(text)
