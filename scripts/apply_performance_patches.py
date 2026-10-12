"""Enable existing spatial shaders and add asynchronous renderer diagnostics."""
from pathlib import Path
import shutil
import runpy

root=Path('tools/rexglue-sdk')
target=root/'include/rex/ui/gh2_render_timing.h'
contents=Path('runtime/render_timing.h').read_bytes()
if not target.exists() or target.read_bytes()!=contents:target.write_bytes(contents)

def replace(relative,before,after):
    file=root/relative;source=file.read_text()
    if after in source:return
    if 'renderer_draw_cpu_including_waits' in after and 'renderer_draw_sampled_cpu' in source:return
    if before not in source:raise RuntimeError(f'Pinned performance patch pattern changed: {relative}: {before[:100]}')
    file.write_text(source.replace(before,after,1))

# Disable the spatial backend in every ABI consumer. Old saved FSR/CAS
# values are rejected by the bilinear-only registration and use its default.
for relative in ('include/rex/ui/presenter.h',):
    file=root/relative;source=file.read_text().replace('#define GH2_SPATIAL_UPSCALING 1','#undef GH2_SPATIAL_UPSCALING')
    file.write_text(source) if source!=file.read_text() else None

for relative in ('include/rex/ui/d3d12/d3d12_presenter.h','include/rex/graphics/d3d12/command_processor.h'):
    replace(relative,'#include <array>','#include <array>\n#include <rex/ui/gh2_render_timing.h>')
replace('include/rex/ui/d3d12/d3d12_presenter.h','  const D3D12Provider& provider_;','  gh2::profiling::GpuTimer gh2_gpu_timer_{"present_gpu"};\n  const D3D12Provider& provider_;')
replace('include/rex/graphics/d3d12/command_processor.h',' private:',' private:\n  gh2::profiling::GpuTimer gh2_gpu_timer_{"renderer_gpu_submission"};')

p='src/ui/d3d12/d3d12_presenter.cpp'
replace(p,'  // Begin the command list with the command allocator not currently potentially','  static gh2::profiling::Metric gh2_paint_cpu("present_cpu_including_waits");\n  gh2::profiling::Scope gh2_paint_scope(gh2_paint_cpu);\n  // Begin the command list with the command allocator not currently potentially')
replace(p,'  command_list->Reset(command_allocator, nullptr);','  command_list->Reset(command_allocator, nullptr);\n  gh2_gpu_timer_.Begin(provider_.GetDevice(),provider_.GetDirectQueue(),paint_context_.paint_submission_tracker.GetCompletedSubmission(),command_list);')
replace(p,'  command_list->Close();\n  ID3D12CommandList* execute_command_list = command_list;','  gh2_gpu_timer_.End(command_list,current_paint_submission);\n  command_list->Close();\n  ID3D12CommandList* execute_command_list = command_list;')
replace(p,'    if (guest_output_flow.effect_count) {\n      ID3D12DescriptorHeap* view_heap', '''    if (guest_output_flow.effect_count) {
      static std::array<uint32_t,4> previous_size{};
      const std::array<uint32_t,4> size{guest_output_properties.frontbuffer_width,guest_output_properties.frontbuffer_height,paint_context_.swap_chain_width,paint_context_.swap_chain_height};
      if(size!=previous_size){previous_size=size;REXLOG_INFO("GH2 presentation: input={}x{} output={}x{} effect={} passes={}",size[0],size[1],size[2],size[3],rex::cvar::GetFlagByName("present_effect"),guest_output_flow.effect_count);}
      ID3D12DescriptorHeap* view_heap''')
replace(p,'  HRESULT present_result = paint_context_.swap_chain->Present(policy.interval,policy.flags);','  const auto gh2_present_begin=gh2::profiling::Clock::now();\n  HRESULT present_result = paint_context_.swap_chain->Present(policy.interval,policy.flags);\n  static gh2::profiling::Metric gh2_present_metric("dxgi_present_cpu");\n  gh2_present_metric.Add(std::chrono::duration<double,std::milli>(gh2::profiling::Clock::now()-gh2_present_begin).count());')
replace(p,'    paint_context_.paint_submission_tracker.AwaitSubmissionCompletion(current_paint_submission -','    static gh2::profiling::Metric metric("present_allocator_wait_cpu");gh2::profiling::Scope scope(metric);\n    paint_context_.paint_submission_tracker.AwaitSubmissionCompletion(current_paint_submission -')

p='src/graphics/d3d12/command_processor.cpp'
replace(p,'void D3D12CommandProcessor::CheckSubmissionFence(uint64_t await_submission) {','void D3D12CommandProcessor::CheckSubmissionFence(uint64_t await_submission) {\n  static gh2::profiling::Metric metric("renderer_fence_cpu");gh2::profiling::Scope scope(metric);')
replace(p,'bool D3D12CommandProcessor::EndSubmission(bool is_swap) {','bool D3D12CommandProcessor::EndSubmission(bool is_swap) {\n  static gh2::profiling::Metric metric("renderer_submit_cpu");gh2::profiling::Scope scope(metric);')
replace(p,'                                      bool major_mode_explicit) {','                                      bool major_mode_explicit) {\n  static gh2::profiling::Metric metric("renderer_draw_cpu_including_waits");gh2::profiling::Scope scope(metric);')
replace(p,'    pipeline_cache_->EndSubmission();','    { static gh2::profiling::Metric pipeline_metric("renderer_pipeline_cpu");gh2::profiling::Scope pipeline_scope(pipeline_metric);\n      pipeline_cache_->EndSubmission(); }')
replace(p,'    command_list_->Reset(command_allocator, nullptr);\n    deferred_command_list_.Execute(command_list_, command_list_1_);','    command_list_->Reset(command_allocator, nullptr);\n    gh2_gpu_timer_.Begin(provider.GetDevice(),direct_queue,submission_completed_,command_list_);\n    deferred_command_list_.Execute(command_list_, command_list_1_);\n    gh2_gpu_timer_.End(command_list_,submission_current_);')

# Upgrade earlier all-draw profiling without nesting a second scope.
p=root/'src/graphics/d3d12/command_processor.cpp';source=p.read_text()
source=source.replace('static gh2::profiling::Metric metric("renderer_draw_cpu_including_waits");gh2::profiling::Scope scope(metric);',
    'static gh2::profiling::Metric metric("renderer_draw_sampled_cpu");static uint32_t draw_sequence=0;gh2::profiling::SampledScope scope(metric,draw_sequence);')
p.write_text(source)
runpy.run_path('scripts/apply_temporal_patches.py')
