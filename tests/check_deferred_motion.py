from pathlib import Path
import json,os,subprocess,sys
root=Path(__file__).resolve().parents[1]
chain='--chain' in sys.argv
name='motion_chain' if chain else 'deferred_motion'
compiler=root/'tools/llvm-mingw-20261006-ucrt-x86_64/bin/clang++.exe';binary=root/f'build/performance-stage/{name}_test.exe'
args=[str(compiler),'-std=c++23','-O2','-msse4.1','-mavx','-DNOMINMAX','-DWIN32_LEAN_AND_MEAN','-DGH2_DEFERRED_MOTION_TEST','-D_WIN32_WINNT=0x0A00','-DNTDDI_VERSION=0x0A000000',
    '-I'+str(root/'tools/rexglue-sdk/thirdparty/xxHash'),'@../../CMakeFiles/gh2_rex_gpu_smoke.dir/includes_CXX.rsp',str(root/'tests/motion_raster.cpp'),
    'CMakeFiles/rexgpu-xenos.dir/objects.a','@CMakeFiles/rexgpu-xenos.dir/linkLibs.rsp','-ld3dcompiler','-o',str(binary)]
if chain:args.insert(1,'-DGH2_MOTION_CHAIN_TEST')
result=subprocess.run(args,cwd=root/'build/rexglue/src/graphics',capture_output=True,text=True)
(root/'build/deferred_motion_compile.log').write_text(result.stdout+result.stderr)
if result.returncode:raise RuntimeError((result.stdout+result.stderr)[-2500:])
env=dict(os.environ);env['PATH']=str(binary.parent)+os.pathsep+str(compiler.parent)+os.pathsep+env['PATH']
result=subprocess.run([str(binary)],env=env,cwd=root,capture_output=True,text=True,timeout=45);print(result.stdout);print(result.stderr)
(root/f'docs/{name}_validation.json').write_text(json.dumps(dict(result='pass' if result.returncode==0 else 'failed',stdout=result.stdout,stderr=result.stderr,scope='Native SDK deferred compute-to-raster chain from synthetic vertex pairs.' if chain else 'Native SDK deferred motion raster from synthetic clip pairs.',limitations='No guest draw coverage, object certification or complete title motion validated.'),indent=2))
if result.returncode:raise RuntimeError('Deferred motion fixture failed')
