"""Guarded opt-in native depth/history hooks; spatial presentation stays default."""
from pathlib import Path
root=Path('tools/rexglue-sdk')
for name in ('depth_conversion.h','deferred_depth.h','depth_frame_pool.h','geometry_history.h','vertex_frame_pool.h','temporal_renderer.h','linear_motion_clips.h','clip_frame_pool.h'):
    target=root/'include/rex/ui'/name;source=Path('runtime',name).read_bytes()
    if not target.exists()or target.read_bytes()!=source:target.write_bytes(source)
def replace(relative,before,after):
    file=root/relative;source=file.read_text()
    if after in source:return
    if before=='namespace rex::graphics::d3d12 {' and 'void D3D12CommandProcessor::GH2RecordDepthSnapshot(unsigned layer)' in source:return
    if before=='  gh2_temporal_capture_.Swap(guest_output_width,guest_output_height);' and 'const auto stats=gh2_renderer_inputs_.history.Stats();' in source:return
    if before=='  if(gh2_temporal_capture_.WantsDraw()) {' and 'gh2::temporal::GeometryKey key;' in source:return
    if before=='      state.previous_vertices_preserved=false;' and 'Preserving raw bytes is necessary but not sufficient for safe motion:' in source:return
    if before=='    gh2_renderer_inputs_.history.EndFrame();--gh2_renderer_inputs_.frames;' and 'gh2_renderer_inputs_.vertices.EndFrame();' in source:return
    if before not in source:raise RuntimeError('Pinned temporal renderer patch changed: '+relative)
    file.write_text(source.replace(before,after,1))
replace('src/graphics/CMakeLists.txt','        d3d12\n        dxgi','        d3d12\n        d3dcompiler\n        dxgi')
replace('include/rex/graphics/d3d12/command_processor.h','#include <rex/ui/gh2_temporal_capture.h>',
        '#include <rex/ui/gh2_temporal_capture.h>\n#include <rex/ui/temporal_renderer.h>')
replace('include/rex/graphics/d3d12/command_processor.h','  gh2::temporal::Capture gh2_temporal_capture_;',
        '  gh2::temporal::Capture gh2_temporal_capture_;\n  gh2::temporal::RendererInputs gh2_renderer_inputs_;\n  void GH2RecordDepthSnapshot(unsigned layer);')
replace('include/rex/graphics/d3d12/render_target_cache.h',
        '  // Metadata only: no resource transitions, readback or additional GPU work.',
        '''  auto GH2CurrentDepthTarget() const {
    if(GetPath()!=Path::kHostRenderTargets || !are_current_command_list_render_targets_valid_ ||
       !(GetLastUpdateBoundRenderTargets()&1))return static_cast<D3D12RenderTarget*>(nullptr);
    return const_cast<D3D12RenderTarget*>(static_cast<const D3D12RenderTarget*>(current_command_list_render_targets_[0]));
  }
  // Metadata only: no resource transitions, readback or additional GPU work.''')
p='src/graphics/d3d12/command_processor.cpp'
replace(p,'namespace rex::graphics::d3d12 {', '''REXCVAR_DEFINE_COMMAND(gh2_probe_temporal_renderer, [](){
  gh2::temporal::renderer_probe_frames.store(16);
}, "GPU/Diagnostics", "Opt-in depth/history producer probe for 16 frames; does not enable DLSS");

namespace rex::graphics::d3d12 {
void D3D12CommandProcessor::GH2RecordDepthSnapshot(unsigned layer) {
  auto& input=gh2_renderer_inputs_;
  if(!layer||layer>=input.depth_recorded.size()||input.depth_recorded[layer])return;
  input.depth_recorded[layer]=true;
  auto* depth=render_target_cache_->GH2CurrentDepthTarget();if(!depth)return;
  auto* resource=depth->resource();const auto desc=resource->GetDesc();
  const auto& vp=input.viewport;
  const float scale=(vp.z_max-vp.z_min)*vp.ndc_scale[2];
  const float offset=vp.z_min+(vp.z_max-vp.z_min)*vp.ndc_offset[2];
  gh2::temporal::DepthRegion region{vp.xy_offset[0],vp.xy_offset[1],vp.xy_extent[0],vp.xy_extent[1],scale,offset};
  // Never publish an incomplete depth surface as a complete color-frame input.
  if(uint64_t(region.left)+region.width>desc.Width||uint64_t(region.top)+region.height>desc.Height||
     !region.width||!region.height||!std::isfinite(scale)||std::abs(scale)<1e-9) {
    REXLOG_WARN("GH2 temporal depth: layer={} rejected crop={}x{}+{},{} allocation={}x{}",layer,region.width,region.height,region.left,region.top,desc.Width,desc.Height);return;
  }
  try {
    auto* device=GetD3D12Provider().GetDevice();
    if(!input.initialized){input.converter.Initialize(device);input.initialized=true;}
    auto* slot=input.pool.Acquire(device,submission_completed_,submission_current_,region.width,region.height,depth->descriptor_srv().GetHandle());
    if(!slot){REXLOG_WARN("GH2 temporal depth: layer={} busy/failed slot; skipped without wait",layer);return;}
    const auto old=depth->SetResourceState(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    PushTransitionBarrier(resource,old,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);SubmitBarriers();
    gh2::temporal::DeferredDepthCommands commands(deferred_command_list_);
    const bool recorded=input.converter.Record(&commands,resource,slot->output.Get(),slot->heap.Get(),slot->heap->GetGPUDescriptorHandleForHeapStart(),region);
    // Restore depth and cached graphics state before subsequent guest draws.
    PushTransitionBarrier(resource,depth->SetResourceState(old),old);SubmitBarriers();
    deferred_command_list_.SetDescriptorHeaps(bindless_resources_used_?view_bindless_heap_:view_bindful_heap_current_,
        bindless_resources_used_?sampler_bindless_heap_current_:sampler_bindful_heap_current_);
    current_guest_pipeline_=nullptr;current_external_pipeline_=nullptr;current_graphics_root_up_to_date_=0;
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;barrier.UAV.pResource=slot->output.Get();
    deferred_command_list_.D3DResourceBarrier(1,&barrier);
    REXLOG_INFO("GH2 temporal depth: layer={} recorded={} output={}x{} submission={} (not yet DLSS-valid)",layer,recorded,region.width,region.height,submission_current_);
  }catch(const std::exception& error){REXLOG_ERROR("GH2 temporal depth: producer failed: {}",error.what());}
}''')
replace(p,'  gh2_temporal_capture_.Swap(guest_output_width,guest_output_height);', '''  if(gh2_renderer_inputs_.frames) {
    const auto stats=gh2_renderer_inputs_.history.Stats();
    REXLOG_INFO("GH2 temporal history: unique={} ambiguous={} verified_previous={} overflow={}",stats.unique,stats.ambiguous,stats.valid_previous,stats.overflow);
    gh2_renderer_inputs_.history.EndFrame();--gh2_renderer_inputs_.frames;
  }
  gh2_renderer_inputs_.BeginFrame();
  gh2_temporal_capture_.Swap(guest_output_width,guest_output_height);''')
replace(p,'  if (!render_target_cache_->Update(is_rasterization_done, normalized_depth_control,', '''  if(gh2_renderer_inputs_.frames) {
    const unsigned layer=gh2::temporal::Layer(regs[XE_GPU_REG_PA_CL_VPORT_ZSCALE]);
    const unsigned previous=gh2_renderer_inputs_.last_layer;
    if((previous==1&&layer==2)||(previous==2&&layer==3))GH2RecordDepthSnapshot(previous);
  }
  if (!render_target_cache_->Update(is_rasterization_done, normalized_depth_control,''')
replace(p,'  if(gh2_temporal_capture_.WantsDraw()) {', '''  if(gh2_renderer_inputs_.frames) {
    const unsigned layer=gh2::temporal::Layer(regs[XE_GPU_REG_PA_CL_VPORT_ZSCALE]);
    gh2_renderer_inputs_.last_layer=layer;gh2_renderer_inputs_.viewport=viewport_info;
    if(layer) {
      gh2::temporal::GeometryKey key;
      key.shader=vertex_shader->ucode_data_hash();key.layer=layer;
      key.index_base=primitive_processing_result.guest_index_base;
      key.index_count=primitive_processing_result.host_draw_vertex_count;
      const auto& map=vertex_shader->constant_register_map();
      for(uint32_t i=0;i<96;++i)if(map.vertex_fetch_bitmap[i/32]&(uint32_t(1)<<(i%32))) {
        key.fetch_descriptors.push_back(i);key.fetch_descriptors.push_back(regs[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0+2*i]);key.fetch_descriptors.push_back(regs[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0+2*i+1]);
      }
      gh2::temporal::GeometryState state;
      std::memcpy(state.constants.data(),&regs[XE_GPU_REG_SHADER_CONSTANT_000_X],sizeof(state.constants));
      // Constant history alone cannot prove that a reused guest vertex buffer
      // retains its previous animated contents. Keep this false until preserved.
      state.previous_vertices_preserved=false;
      gh2_renderer_inputs_.history.Record(std::move(key),std::move(state));
    }
  }
  if(gh2_temporal_capture_.WantsDraw()) {''')

replace(p,'    gh2_renderer_inputs_.history.EndFrame();--gh2_renderer_inputs_.frames;', '''    if(auto* vertices=gh2_renderer_inputs_.vertices.Active()) {
      PushTransitionBarrier(vertices->buffer.Get(),vertices->state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
      vertices->state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;SubmitBarriers();
    }
    gh2_renderer_inputs_.vertices.EndFrame();
    gh2_renderer_inputs_.history.EndFrame();--gh2_renderer_inputs_.frames;''')
replace(p,'      state.previous_vertices_preserved=false;', '''      state.previous_vertices_preserved=false;
      if(!memexport_used) {
        auto* slot=gh2_renderer_inputs_.vertices.Begin(GetD3D12Provider().GetDevice(),submission_completed_);
        if(slot) {
          if(slot->state!=D3D12_RESOURCE_STATE_COPY_DEST) {
            PushTransitionBarrier(slot->buffer.Get(),slot->state,D3D12_RESOURCE_STATE_COPY_DEST);
            slot->state=D3D12_RESOURCE_STATE_COPY_DEST;
          }
          bool copying=false;
          for(uint32_t i=0;i<96;++i)if(map.vertex_fetch_bitmap[i/32]&(uint32_t(1)<<(i%32))) {
            const auto fetch=regs.GetVertexFetch(i);
            if(fetch.type!=xenos::FetchConstantType::kVertex)continue;
            bool needed=false;
            const auto range=gh2_renderer_inputs_.vertices.Reserve(fetch.address<<2,fetch.size<<2,submission_current_,needed);
            if(range&&needed) {
              if(!copying){shared_memory_->UseAsCopySource();SubmitBarriers();copying=true;}
              deferred_command_list_.D3DCopyBufferRegion(slot->buffer.Get(),range->offset,shared_memory_->GetBuffer(),range->source,range->size);
            }
          }
          if(copying){shared_memory_->UseForReading();SubmitBarriers();}
        }
      }
      // Preserving raw bytes is necessary but not sufficient for safe motion:
      // stable per-object identity and vertex/index correspondence are pending.''')
replace(p,'void D3D12CommandProcessor::ShutdownContext() {\n  AwaitAllQueueOperationsCompletion();', '''void D3D12CommandProcessor::ShutdownContext() {
  AwaitAllQueueOperationsCompletion();
  gh2_renderer_inputs_.pool.ClearAfterGpuDrain();
  gh2_renderer_inputs_.vertices.ClearAfterGpuDrain();
  gh2_renderer_inputs_.history.Reset();''')

import runpy
runpy.run_path('scripts/update_depth_tiles.py')
runpy.run_path('scripts/apply_linear_position_patches.py')
runpy.run_path('scripts/apply_native_motion_patches.py')
runpy.run_path('scripts/fix_temporal_frame_boundary.py')
