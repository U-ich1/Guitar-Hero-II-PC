from pathlib import Path
import json,os,subprocess
root=Path(__file__).resolve().parents[1]
compiler=root/'tools/llvm-mingw-20261006-ucrt-x86_64/bin/clang++.exe';binary=root/'build/geometry_history_test.exe'
subprocess.run([str(compiler),'-std=c++20','-O2','-I'+str(root/'runtime'),str(root/'tests/geometry_history.cpp'),'-o',str(binary)],check=True)
env=dict(os.environ);env['PATH']=str(compiler.parent)+os.pathsep+env['PATH']
result=subprocess.run([str(binary)],env=env,capture_output=True,text=True,check=True)
print(result.stdout)
(root/'docs/geometry_history_validation.json').write_text(json.dumps(dict(result='pass',stdout=result.stdout,scope='Bounded constant-history association and invalidation. No previous vertex buffers, native draw correspondence or rasterized game motion validated.'),indent=2))
