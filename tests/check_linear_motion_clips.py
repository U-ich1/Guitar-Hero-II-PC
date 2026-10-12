from pathlib import Path
import os,json,subprocess,argparse
root=Path(__file__).resolve().parents[1]
compiler=root/'tools/llvm-mingw-20261006-ucrt-x86_64/bin/clang++.exe';binary=root/'build/performance-stage/linear_motion_clips_test.exe'
parser=argparse.ArgumentParser();parser.add_argument('--deferred',action='store_true');parser.add_argument('--mapped',action='store_true');parser.add_argument('--candidates',action='store_true',help='Compare additional original shaders with the existing kernel before enabling them');parser.add_argument('--skinned',action='store_true');options=parser.parse_args()
args=[str(compiler),'-std=c++23','-O2','-msse4.1','-mavx','-DNOMINMAX','-DWIN32_LEAN_AND_MEAN','-D_WIN32_WINNT=0x0A00','-DNTDDI_VERSION=0x0A000000',
    '-I'+str(root/'tools/rexglue-sdk/thirdparty/xxHash'),'@../../CMakeFiles/gh2_rex_gpu_smoke.dir/includes_CXX.rsp',str(root/'tests/linear_motion_clips.cpp'),
    'CMakeFiles/rexgpu-xenos.dir/objects.a','@CMakeFiles/rexgpu-xenos.dir/linkLibs.rsp','-ld3dcompiler','-o',str(binary)]
if options.deferred:args.insert(1,'-DGH2_DEFERRED_LINEAR_TEST')
if options.mapped:args.insert(1,'-DGH2_MAPPED_LINEAR_TEST')
compiled=subprocess.run(args,cwd=root/'build/rexglue/src/graphics',capture_output=True,text=True)
(root/'build/linear_motion_clips_compile.log').write_text(compiled.stdout+compiled.stderr)
if compiled.returncode:raise RuntimeError((compiled.stdout+compiled.stderr)[-2500:])
env=dict(os.environ);env['PATH']=str(binary.parent)+os.pathsep+str(compiler.parent)+os.pathsep+env['PATH']
cache=root/'build/windowed-state/cache/shaders/shareable/415607E7.xsh'
# Keep a local, excluded-from-releases oracle snapshot so future comparisons
# do not require closing a running game. The C++ reader verifies every hash.
snapshot=root/'build/temporal-shaders/oracle-cache.xsh'
try:
    cache_bytes=cache.read_bytes()
except PermissionError:
    if not snapshot.exists():raise RuntimeError('Shader cache is locked and no offline oracle snapshot exists yet')
else:
    snapshot.parent.mkdir(parents=True,exist_ok=True)
    snapshot.write_bytes(cache_bytes)
shaders=['2eba09b0974055eb']
if options.candidates:shaders+=['cb9d92c934e26a03','8f72b1ddc2edc8e1']
if options.skinned:shaders=['b6d10de26caf4b00','47b0406889d62288','17c6d38225de0653','72180349f1ac86ee','86495a03257c4f49']
results=[]
for shader in shaders:
    result=subprocess.run([str(binary),str(snapshot),shader],cwd=root,env=env,capture_output=True,text=True,timeout=45)
    results.append(dict(shader=shader,returncode=result.returncode,stdout=result.stdout,stderr=result.stderr))
    print(shader,result.stdout,result.stderr)
    if result.returncode:break

print(result.stdout);print(result.stderr)
(root/'docs'/('skinned_motion_validation.json' if options.skinned else 'candidate_linear_motion_validation.json' if options.candidates else 'mapped_linear_motion_validation.json' if options.mapped else 'linear_motion_clips_validation.json')).write_text(json.dumps(dict(result='pass' if result.returncode==0 else 'failed',deferred=options.deferred,mapped=options.mapped,shaders=results,scope='Actual GPU clip generation compared with original cached GH2 shader programs. Candidate mode tests equivalence only; it does not enable additional native shaders. Native object matching, skinning and complete motion remain unvalidated.'),indent=2))
if result.returncode:raise RuntimeError('Title position program comparison failed')
