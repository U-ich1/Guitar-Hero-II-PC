"""Compile/run the isolated D3D12 timestamp and spatial pipeline fixture."""
from pathlib import Path
import argparse,os,subprocess,json
root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser();parser.add_argument('--directory',type=Path,default=root/'tools/rexglue-sdk/out/win-amd64');parser.add_argument('--compile-only',action='store_true');args=parser.parse_args();directory=args.directory.resolve();directory.mkdir(parents=True,exist_ok=True)
compiler=root/'tools/llvm-mingw-20261006-ucrt-x86_64/bin/clang++.exe';binary=directory/'render_timing_test.exe'
command=[str(compiler),'-std=c++23','-msse4.1','-mavx','-DNOMINMAX','-DWIN32_LEAN_AND_MEAN','-D_WIN32_WINNT=0x0A00','-DNTDDI_VERSION=0x0A000000','@CMakeFiles/gh2_rex_gpu_smoke.dir/includes_CXX.rsp',str(root/'tests/render_timing.cpp'),str(root/'tools/rexglue-sdk/out/win-amd64/librexruntime.dll.a'),'-o',str(binary)]
result=subprocess.run(command,cwd=root/'build/rexglue',capture_output=True,text=True);(root/'build/render_timing_compile.log').write_text(result.stdout+result.stderr)
if result.returncode:raise RuntimeError((result.stdout+result.stderr)[-3000:])
if not args.compile_only:
    env=dict(os.environ);env["PATH"]=str(directory)+os.pathsep+str(compiler.parent)+os.pathsep+env["PATH"]
    result=subprocess.run([str(binary)],cwd=root,env=env,capture_output=True,text=True,timeout=30)
    if result.returncode:raise RuntimeError(result.stdout+result.stderr)
    report=dict(result='pass',stdout=result.stdout,scope='Real D3D12 bilinear pipeline, retired upscaler rejection, sampled CPU profiling and asynchronous timestamp ring behavior. No game, visible window, song benchmark or image-quality check.')
    (root/'docs/render_timing_validation.json').write_text(json.dumps(report,indent=2));print(result.stdout)
