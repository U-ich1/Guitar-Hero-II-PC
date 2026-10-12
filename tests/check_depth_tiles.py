from pathlib import Path
import os,subprocess,json
root=Path(__file__).resolve().parents[1]
compiler=root/'tools/llvm-mingw-20261006-ucrt-x86_64/bin/clang++.exe';binary=root/'build/depth_tiles_test.exe'
subprocess.run([str(compiler),'-std=c++20','-O2','-DNOMINMAX','-DWIN32_LEAN_AND_MEAN','-I'+str(root/'runtime'),str(root/'tests/depth_tiles.cpp'),'-ld3d12','-ld3dcompiler','-o',str(binary)],check=True)
env=dict(os.environ);env['PATH']=str(compiler.parent)+os.pathsep+env['PATH']
result=subprocess.run([str(binary)],env=env,capture_output=True,text=True,check=True);print(result.stdout)
(root/'docs/depth_tiles_validation.json').write_text(json.dumps(dict(result='pass',stdout=result.stdout,scope='Tile placement and coverage using observed dimensions and explicit window offsets. Native depth pixel quality and final-color correlation still need validation.'),indent=2))
