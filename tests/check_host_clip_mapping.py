from pathlib import Path
import json,os,subprocess
root=Path(__file__).resolve().parents[1]
compiler=root/'tools/llvm-mingw-20261006-ucrt-x86_64/bin/clang++.exe';binary=root/'build/host_clip_mapping_test.exe'
subprocess.run([str(compiler),'-std=c++20','-O2','-I'+str(root/'runtime'),str(root/'tests/host_clip_mapping.cpp'),'-o',str(binary)],check=True)
env=dict(os.environ);env['PATH']=str(compiler.parent)+os.pathsep+env['PATH']
result=subprocess.run([str(binary)],env=env,capture_output=True,text=True,check=True);print(result.stdout)
(root/'docs/host_clip_mapping_validation.json').write_text(json.dumps(dict(result='pass',stdout=result.stdout,scope='Coordinate conversion rules based on native shader completion and explicit tiled viewport examples. No native velocity field or image alignment validated.'),indent=2))
