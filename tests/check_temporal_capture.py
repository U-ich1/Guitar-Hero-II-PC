"""Exercise actual recorder serialization and bounded state, without a game."""
from pathlib import Path
import json,os,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
# Check guarded patch idempotence and that one guest swap produces one capture frame.
patcher=root/'scripts/apply_temporal_patches.py'
subprocess.run([__import__('sys').executable,str(patcher)],cwd=root,check=True)
patched=root/'tools/rexglue-sdk/src/graphics/d3d12/command_processor.cpp'
before=patched.read_bytes()
subprocess.run([__import__('sys').executable,str(patcher)],cwd=root,check=True)
assert patched.read_bytes()==before
assert patched.read_text().count('gh2_temporal_capture_.Swap(')==1
assert patched.read_text().count('gh2_renderer_inputs_.vertices.EndFrame();')==1
assert patched.read_text().count('GH2GeneratePositionCandidates();')==1
assert 'Request(submission_current_,gh2::temporal::kPositionConstantBytes,256' in patched.read_text()
assert '{gh2::temporal::PositionVertexStride(key.shader),key.index_count' in patched.read_text()
assert '{before.linear.index_max,uint32_t(gh2::temporal::SupportsSkinnedPositionShader(key.shader)),0,0}' in patched.read_text()
compiler=root/'tools/llvm-mingw-20261006-ucrt-x86_64/bin/clang++.exe'
binary=root/'build/temporal_capture_test.exe'
subprocess.run([str(compiler),'-std=c++20','-O2','-fno-fast-math','-I'+str(root/'runtime'),str(root/'tests/temporal_capture.cpp'),'-o',str(binary)],check=True)
env=dict(os.environ);env['PATH']=str(compiler.parent)+os.pathsep+env['PATH']
with tempfile.TemporaryDirectory(prefix='gh2-temporal-',dir=root/'build') as directory:
    result=subprocess.run([str(binary),directory],env=env,capture_output=True,text=True,timeout=30,check=True)
    lines=[json.loads(line) for line in (Path(directory)/'complete.jsonl').read_text().splitlines()]
    assert lines[0]['complete'] and not lines[0]['validated_dlss_inputs']
    assert len(lines)==17
    assert [f['frame'] for f in lines[1:]]==list(range(1,17))
    for frame in lines[1:]:
        assert frame['input']==[1280,720] and frame['seen_draws']==2055 and len(frame['draws'])==2048
        draw=frame['draws'][0]
        assert draw['vs']==0xfedcba9876543210 and draw['depth_resource']==[2560,1440,0,2]
        assert draw['constants']==[[3,0x7fc12345,0x80000000,0x3f800000,0]]
        assert draw['fetches']==[[4,123,456]]
    partial=[json.loads(line) for line in (Path(directory)/'partial.jsonl').read_text().splitlines()]
    assert not partial[0]['complete'] and len(partial)==2
    assert not (Path(directory)/'complete.jsonl.partial').exists()
report={'result':'pass','stdout':result.stdout,'scope':'Bounded CPU-only temporal candidate capture and raw-bit serialization. No game, GPU depth pixels, motion vectors, DLSS initialization or reconstruction tested.'}
(root/'docs/temporal_capture_validation.json').write_text(json.dumps(report,indent=2))
print(result.stdout)
