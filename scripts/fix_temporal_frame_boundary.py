"""Repair the older layered-patch duplicate; one boundary retains prior bytes."""
from pathlib import Path
p=Path('tools/rexglue-sdk/src/graphics/d3d12/command_processor.cpp');text=p.read_text()
duplicate='''    GH2GeneratePositionCandidates();
    gh2_renderer_inputs_.vertices.EndFrame();
    if(auto* vertices=gh2_renderer_inputs_.vertices.Active()) {
      PushTransitionBarrier(vertices->buffer.Get(),vertices->state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
      vertices->state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;SubmitBarriers();
    }
    gh2_renderer_inputs_.vertices.EndFrame();'''
if duplicate in text:
    text=text.replace(duplicate,'''    GH2GeneratePositionCandidates();
    gh2_renderer_inputs_.vertices.EndFrame();''',1)
    p.write_text(text)
if text.count('gh2_renderer_inputs_.vertices.EndFrame();')!=1:
    raise RuntimeError('Native frame boundary must advance vertex history exactly once')
if text.count('GH2GeneratePositionCandidates();')!=1:
    raise RuntimeError('Native frame boundary must generate positions exactly once')
