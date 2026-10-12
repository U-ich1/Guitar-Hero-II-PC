from pathlib import Path
import json,os,subprocess
root=Path(__file__).resolve().parents[1]
compiler=root/'tools/llvm-mingw-20261006-ucrt-x86_64/bin/clang++.exe';binary=root/'build/temporal_geometry_test.exe'
subprocess.run([str(compiler),'-std=c++20','-O2','-I'+str(root/'runtime'),str(root/'tests/temporal_geometry.cpp'),'-o',str(binary)],check=True)
env=dict(os.environ);env['PATH']=str(compiler.parent)+os.pathsep+env['PATH']
result=subprocess.run([str(binary)],env=env,capture_output=True,text=True,check=True)
print(result.stdout)
(root/'docs/temporal_geometry_validation.json').write_text(json.dumps(dict(result='pass',stdout=result.stdout,scope='Mathematical helpers using independently specified projection, motion, jitter and observed depth ranges. No game shader injection, rasterized motion vectors or DLSS reconstruction.'),indent=2))
