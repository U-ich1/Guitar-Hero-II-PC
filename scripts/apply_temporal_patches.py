"""Add bounded, opt-in observations for the temporal-upscaling investigation."""
from pathlib import Path

root=Path('tools/rexglue-sdk')
target=root/'include/rex/ui/gh2_temporal_capture.h'
contents=Path('runtime/temporal_capture.h').read_bytes()
if not target.exists() or target.read_bytes()!=contents:target.write_bytes(contents)

def replace(relative,before,after):
    file=root/relative;source=file.read_text()
    if after in source:return
    if before=='namespace rex::graphics::d3d12 {' and 'static gh2::temporal::Draw GH2SnapshotTemporalCandidates(' in source:return
    if before not in source:raise RuntimeError(f'Pinned temporal patch pattern changed: {relative}')
    file.write_text(source.replace(before,after,1))

replace('include/rex/graphics/d3d12/command_processor.h',
        '#include <rex/ui/gh2_render_timing.h>',
        '#include <rex/ui/gh2_render_timing.h>\n#include <rex/ui/gh2_temporal_capture.h>')
replace('include/rex/graphics/d3d12/command_processor.h',
        '  gh2::profiling::GpuTimer gh2_gpu_timer_{"renderer_gpu_submission"};',
        '  gh2::profiling::GpuTimer gh2_gpu_timer_{"renderer_gpu_submission"};\n  gh2::temporal::Capture gh2_temporal_capture_;')

replace('include/rex/graphics/d3d12/render_target_cache.h',
        '  const RenderTarget* const*\n      current_command_list_render_targets_[1 + xenos::kMaxColorRenderTargets];',
        '  const RenderTarget*\n      current_command_list_render_targets_[1 + xenos::kMaxColorRenderTargets];')

replace('include/rex/graphics/d3d12/render_target_cache.h',
        '  bool depth_float24_round() const { return depth_float24_round_; }',
        '''  // Metadata only: no resource transitions, readback or additional GPU work.
  D3D12_RESOURCE_DESC GH2CurrentDepthDescription() const {
    if (GetPath() != Path::kHostRenderTargets ||
        !are_current_command_list_render_targets_valid_ ||
        !(GetLastUpdateBoundRenderTargets() & 1) || !current_command_list_render_targets_[0]) return {};
    return static_cast<const D3D12RenderTarget*>(current_command_list_render_targets_[0])->resource()->GetDesc();
  }
  bool depth_float24_round() const { return depth_float24_round_; }''')

source='src/graphics/d3d12/command_processor.cpp'
replace(source,'namespace rex::graphics::d3d12 {', '''REXCVAR_DEFINE_COMMAND_ARGS(gh2_capture_dlss_inputs, [](std::string_view args) {
  if(!gh2::temporal::requests.Request(std::filesystem::u8path(args))) {
    REXLOG_WARN("GH2 temporal capture: request rejected (pending request or invalid .jsonl path)");
  }
}, "GPU/Diagnostics", "Capture bounded shader, depth-target and transform candidates for DLSS development");

namespace rex::graphics::d3d12 {
static gh2::temporal::Draw GH2SnapshotTemporalCandidates(
    const RegisterFile& regs, const Shader& vs, const Shader* ps,
    D3D12RenderTargetCache& targets, uint32_t vertices, xenos::PrimitiveType primitive,
    reg::RB_DEPTHCONTROL depth_control, uint32_t color_mask) {
  gh2::temporal::Draw draw;
  draw.vertex_shader=vs.ucode_data_hash();draw.pixel_shader=ps?ps->ucode_data_hash():0;
  draw.vertices=vertices;draw.primitive=uint32_t(primitive);
  draw.depth_control=depth_control.value;draw.depth_info=regs[XE_GPU_REG_RB_DEPTH_INFO];
  draw.surface_info=regs[XE_GPU_REG_RB_SURFACE_INFO];draw.color_mask=color_mask;
  draw.vertex_constant_base=regs[XE_GPU_REG_SQ_VS_CONST];
  for(size_t i=0;i<4;++i)draw.color_info[i]=regs[reg::RB_COLOR_INFO::rt_register_indices[i]];
  for(size_t i=0;i<6;++i)draw.viewport[i]=regs[XE_GPU_REG_PA_CL_VPORT_XSCALE+i];
  draw.scissor={regs[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL],regs[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR]};
  draw.host_targets=targets.GetPath()==RenderTargetCache::Path::kHostRenderTargets;
  const auto depth=targets.GH2CurrentDepthDescription();
  draw.depth_width=depth.Width;draw.depth_height=depth.Height;
  draw.depth_format=uint32_t(depth.Format);draw.depth_samples=depth.SampleDesc.Count;
  const auto& map=vs.constant_register_map();
  // Match the backend's fixed VS constant bank. Preserve raw bits, including NaNs.
  for(uint32_t i=0;i<256;++i)if(map.float_bitmap[i/64]&(uint64_t(1)<<(i%64))) {
    gh2::temporal::Constant c{i,{}};
    for(size_t j=0;j<4;++j)c.bits[j]=regs[XE_GPU_REG_SHADER_CONSTANT_000_X+4*i+j];
    draw.constants.push_back(c);
  }
  for(uint32_t i=0;i<96;++i)if(map.vertex_fetch_bitmap[i/32]&(uint32_t(1)<<(i%32))) {
    draw.fetches.push_back({i,{regs[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0+2*i],regs[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0+2*i+1]}});
  }
  return draw;
}''')

# End the observed frame at IssueSwap, then service a new request so its first
# frame starts at a real guest swap boundary, never halfway through a UI paint.
replace(source,'  presenter->RefreshGuestOutput(\n      guest_output_width,', '''  gh2_temporal_capture_.Swap(guest_output_width,guest_output_height);
  if(const int status=gh2_temporal_capture_.TakeCompletion()) {
    if(status>0)REXLOG_INFO("GH2 temporal capture: background write complete");
    else REXLOG_ERROR("GH2 temporal capture: background write failed; check destination permissions/disk space");
  }
  if(auto requested=gh2::temporal::requests.Take()) {
    if(gh2_temporal_capture_.Start(*requested))
      REXLOG_INFO("GH2 temporal capture: started {} guest frames, max {} draws/frame, output={}",gh2::temporal::Capture::kFrames,gh2::temporal::Capture::kDraws,requested->string());
    else REXLOG_WARN("GH2 temporal capture: already recording or writing; request ignored");
  }
  presenter->RefreshGuestOutput(
      guest_output_width,''')

replace(source,'  return true;\n}\n\nbool D3D12CommandProcessor::IssueDraw_MemexportReadbackFullPath', '''  if(gh2_temporal_capture_.WantsDraw()) {
    gh2_temporal_capture_.AddDraw(GH2SnapshotTemporalCandidates(
        regs,*vertex_shader,pixel_shader,*render_target_cache_,
        primitive_processing_result.host_draw_vertex_count,primitive_type,
        normalized_depth_control,normalized_color_mask));
  }
  return true;
}

bool D3D12CommandProcessor::IssueDraw_MemexportReadbackFullPath''')

import runpy
runpy.run_path('scripts/apply_temporal_renderer_patches.py')
