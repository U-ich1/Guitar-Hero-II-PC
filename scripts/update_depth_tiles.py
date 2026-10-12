"""Upgrade the guarded native producer to the title's repeated EDRAM tiles."""
from pathlib import Path
import runpy
root=Path('tools/rexglue-sdk')
target=root/'include/rex/ui/depth_tiles.h';target.write_bytes(Path('runtime/depth_tiles.h').read_bytes())
p=root/'src/graphics/d3d12/command_processor.cpp';text=p.read_text()
def replace(before,after):
    global text
    if after in text:return
    if before=='      std::memcpy(state.constants.data(),&regs[XE_GPU_REG_SHADER_CONSTANT_000_X],sizeof(state.constants));' and '// Ignore unused float constants; stale banks differ across tile replays.' in text:return
    if before=='gh2_renderer_inputs_.last_layer=layer;gh2_renderer_inputs_.viewport=viewport_info;' and 'gh2_renderer_inputs_.scissor=scissor;' in text:return
    if before not in text:raise RuntimeError('Pinned depth tile upgrade changed: '+before[:70])
    text=text.replace(before,after,1)
replace('  input.depth_recorded[layer]=true;','''  const uint64_t tile=(uint64_t(uint32_t(input.window_x))<<32)|uint32_t(input.window_y);
  for(auto previous_tile:input.depth_tiles[layer])if(previous_tile==tile)return;''')
start='  gh2::temporal::DepthRegion region{vp.xy_offset[0],vp.xy_offset[1],vp.xy_extent[0],vp.xy_extent[1],scale,offset};'
end='  try {\n    auto* device=GetD3D12Provider().GetDevice();'
if 'const auto planned=gh2::temporal::PlanDepthTile(' not in text:
    a=text.index(start);b=text.index(end,a)
    text=text[:a]+'''  const auto planned=gh2::temporal::PlanDepthTile(
      {vp.xy_offset[0],vp.xy_offset[1],vp.xy_extent[0],vp.xy_extent[1]},
      {input.scissor.offset[0],input.scissor.offset[1],input.scissor.extent[0],input.scissor.extent[1]},
      desc.Width,desc.Height,input.window_x,input.window_y,input.scale_x,input.scale_y,input.width,input.height,scale,offset);
  if(!planned){REXLOG_WARN("GH2 temporal depth: layer={} invalid tiled crop/placement; skipped",layer);return;}
  const auto region=*planned;
'''+text[b:]
replace('auto* slot=input.pool.Acquire(device,submission_completed_,submission_current_,region.width,region.height,depth->descriptor_srv().GetHandle());',
'''auto* atlas=input.depth_atlas[layer];
    auto* slot=input.pool.Acquire(device,submission_completed_,submission_current_,input.width,input.height,depth->descriptor_srv().GetHandle(),atlas?atlas->output.Get():nullptr);''')
replace('    const bool recorded=input.converter.Record(&commands,resource,slot->output.Get(),slot->heap.Get(),slot->heap->GetGPUDescriptorHandleForHeapStart(),region);',
'''    if(!atlas) {
      input.depth_atlas[layer]=slot;
      input.converter.Clear(&commands,slot->output.Get(),slot->heap.Get(),slot->heap->GetGPUDescriptorHandleForHeapStart());
      D3D12_RESOURCE_BARRIER clear_barrier{};clear_barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;clear_barrier.UAV.pResource=slot->output.Get();deferred_command_list_.D3DResourceBarrier(1,&clear_barrier);
    }
    const bool recorded=input.converter.Record(&commands,resource,slot->output.Get(),slot->heap.Get(),slot->heap->GetGPUDescriptorHandleForHeapStart(),region);
    if(recorded){input.depth_tiles[layer].push_back(tile);input.depth_regions[layer].push_back(region);}
    input.depth_atlas[layer]->submission=std::max(input.depth_atlas[layer]->submission,submission_current_);
    input.depth_recorded[layer]=gh2::temporal::CompleteDepthTiles(input.depth_regions[layer],input.width,input.height);''')
replace('REXLOG_INFO("GH2 temporal depth: layer={} recorded={} output={}x{} submission={} (not yet DLSS-valid)",layer,recorded,region.width,region.height,submission_current_);',
'''REXLOG_INFO("GH2 temporal depth: layer={} recorded={} tile={}x{}+{},{} atlas={}x{} coverage_complete={} submission={} (not yet DLSS-valid)",layer,recorded,region.width,region.height,region.destination_left,region.destination_top,input.width,input.height,input.depth_recorded[layer],submission_current_);''')
replace('  gh2_renderer_inputs_.BeginFrame();','''  gh2_renderer_inputs_.width=guest_output_width;gh2_renderer_inputs_.height=guest_output_height;
  gh2_renderer_inputs_.BeginFrame();''')
replace('gh2_renderer_inputs_.last_layer=layer;gh2_renderer_inputs_.viewport=viewport_info;',
'''gh2_renderer_inputs_.last_layer=layer;gh2_renderer_inputs_.viewport=viewport_info;
    gh2_renderer_inputs_.scissor=scissor;
    const auto window=regs.Get<reg::PA_SC_WINDOW_OFFSET>();
    gh2_renderer_inputs_.window_x=window.window_x_offset;gh2_renderer_inputs_.window_y=window.window_y_offset;
    gh2_renderer_inputs_.scale_x=draw_resolution_scale_x;gh2_renderer_inputs_.scale_y=draw_resolution_scale_y;''')
replace('      key.shader=vertex_shader->ucode_data_hash();key.layer=layer;',
'''      key.shader=vertex_shader->ucode_data_hash();key.layer=layer;
      key.pixel_shader=pixel_shader?pixel_shader->ucode_data_hash():0;
      key.pass_state={normalized_depth_control.value,normalized_color_mask,regs[XE_GPU_REG_RB_COLOR_INFO],regs[XE_GPU_REG_RB_DEPTH_INFO],regs[XE_GPU_REG_RB_BLENDCONTROL0],regs[XE_GPU_REG_PA_SU_SC_MODE_CNTL]};''')
replace('      std::memcpy(state.constants.data(),&regs[XE_GPU_REG_SHADER_CONSTANT_000_X],sizeof(state.constants));',
'''      // Ignore unused float constants; stale banks differ across tile replays.
      for(uint32_t i=0;i<256;++i)if(map.float_bitmap[i/64]&(uint64_t(1)<<(i%64)))
        std::memcpy(&state.constants[i*4],&regs[XE_GPU_REG_SHADER_CONSTANT_000_X+i*4],4*sizeof(uint32_t));
      state.tile_identity=(uint64_t(uint32_t(window.window_x_offset))<<32)|uint32_t(window.window_y_offset);''')
replace('REXLOG_INFO("GH2 temporal history: unique={} ambiguous={} verified_previous={} overflow={}",stats.unique,stats.ambiguous,stats.valid_previous,stats.overflow);',
'''REXLOG_INFO("GH2 temporal history: unique={} ambiguous={} tile_replays={} verified_previous={} overflow={}",stats.unique,stats.ambiguous,stats.tile_replays,stats.valid_previous,stats.overflow);''')
p.write_text(text)

# Vertex window offsets control tile placement independently from scissor offset
# disabling. Ignore stored offset values when rasterizer window offsets are off.
text=p.read_text()
before='gh2_renderer_inputs_.window_x=window.window_x_offset;gh2_renderer_inputs_.window_y=window.window_y_offset;'
after='''const bool window_enabled=regs.Get<reg::PA_SU_SC_MODE_CNTL>().vtx_window_offset_enable;
    gh2_renderer_inputs_.window_x=window_enabled?window.window_x_offset:0;gh2_renderer_inputs_.window_y=window_enabled?window.window_y_offset:0;'''
if after not in text:
    if before not in text:raise RuntimeError('Window-offset tile patch changed')
    text=text.replace(before,after,1)
before='state.tile_identity=(uint64_t(uint32_t(window.window_x_offset))<<32)|uint32_t(window.window_y_offset);'
after='state.tile_identity=(uint64_t(uint32_t(gh2_renderer_inputs_.window_x))<<32)|uint32_t(gh2_renderer_inputs_.window_y);'
if before in text:text=text.replace(before,after,1)
p.write_text(text)
