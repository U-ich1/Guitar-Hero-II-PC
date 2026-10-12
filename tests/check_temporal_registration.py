from pathlib import Path
import os,subprocess
root=Path(__file__).resolve().parents[1]
compiler=root/'tools/llvm-mingw-20261006-ucrt-x86_64/bin/clang++.exe'
stage=root/'build/performance-stage';binary=stage/'temporal_registration_test.exe'
subprocess.run([str(compiler),'-std=c++23','-msse4.1','-mavx','-DNOMINMAX','-DWIN32_LEAN_AND_MEAN',
    '@CMakeFiles/gh2_rex_gpu_smoke.dir/includes_CXX.rsp',str(root/'tests/temporal_registration.cpp'),
    str(root/'tools/rexglue-sdk/out/win-amd64/librexruntime.dll.a'),'-o',str(binary)],cwd=root/'build/rexglue',check=True)
env=dict(os.environ);env['PATH']=str(stage)+os.pathsep+str(compiler.parent)+os.pathsep+env['PATH']
subprocess.run([str(binary)],cwd=root,env=env,check=True,timeout=15)
