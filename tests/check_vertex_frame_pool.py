from pathlib import Path
import json,os,subprocess
root=Path(__file__).resolve().parents[1]
compiler=root/'tools/llvm-mingw-20261006-ucrt-x86_64/bin/clang++.exe';binary=root/'build/vertex_frame_pool_test.exe'
subprocess.run([str(compiler),'-std=c++20','-O2','-DNOMINMAX','-DWIN32_LEAN_AND_MEAN',str(root/'tests/vertex_frame_pool.cpp'),'-ld3d12','-ldxgi','-o',str(binary)],check=True)
env=dict(os.environ);env['PATH']=str(compiler.parent)+os.pathsep+env['PATH']
result=subprocess.run([str(binary)],env=env,capture_output=True,text=True,timeout=45)
print(result.stdout);print(result.stderr)
(root/'docs/vertex_frame_pool_validation.json').write_text(json.dumps(dict(result='pass' if result.returncode==0 else 'failed',stdout=result.stdout,stderr=result.stderr,scope='Actual GPU preservation of synthetic prior vertex bytes and bounded resource lifetime. No native moving/skinned correspondence or motion field validated.'),indent=2))
if result.returncode:raise RuntimeError('GPU vertex preservation fixture failed')
