from pathlib import Path
import json,os,subprocess
root=Path(__file__).resolve().parents[1]
compiler=root/'tools/llvm-mingw-20261006-ucrt-x86_64/bin/clang++.exe';binary=root/'build/performance-stage/deferred_depth_test.exe'
args=[str(compiler),'-std=c++23','-O2','-msse4.1','-mavx','-DNOMINMAX','-DWIN32_LEAN_AND_MEAN','-DGH2_DEFERRED_DEPTH_TEST',
    '-D_WIN32_WINNT=0x0A00','-DNTDDI_VERSION=0x0A000000','-I'+str(root/'tools/rexglue-sdk/thirdparty/xxHash'),
    '@../../CMakeFiles/gh2_rex_gpu_smoke.dir/includes_CXX.rsp',str(root/'tests/depth_conversion.cpp'),
    'CMakeFiles/rexgpu-xenos.dir/objects.a','@CMakeFiles/rexgpu-xenos.dir/linkLibs.rsp','-ld3dcompiler','-o',str(binary)]
compiled=subprocess.run(args,cwd=root/'build/rexglue/src/graphics',capture_output=True,text=True)
(root/'build/deferred_depth_compile.log').write_text(compiled.stdout+compiled.stderr)
if compiled.returncode:raise RuntimeError((compiled.stdout+compiled.stderr)[-2500:])
env=dict(os.environ);env['PATH']=str(binary.parent)+os.pathsep+str(compiler.parent)+os.pathsep+env['PATH']
result=subprocess.run([str(binary)],cwd=root,env=env,capture_output=True,text=True,timeout=45)
print(result.stdout);print(result.stderr)
report=dict(result='pass' if result.returncode==0 else 'failed',stdout=result.stdout,stderr=result.stderr,scope='Actual native SDK deferred depth dispatch and GPU readback using synthetic depth; bounded fence-protected output slots. No guest draw, real layer boundary or motion texture validated.')
(root/'docs/deferred_depth_validation.json').write_text(json.dumps(report,indent=2))
if result.returncode:raise RuntimeError('Deferred depth fixture failed')
