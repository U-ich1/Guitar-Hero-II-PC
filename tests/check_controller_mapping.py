from pathlib import Path
import subprocess,os,json
root=Path(__file__).resolve().parents[1]
os.chdir(root)
compiler=root/'tools/llvm-mingw-20261006-ucrt-x86_64/bin/clang++.exe'
binary=root/'build/controller_mapping_test.exe'
args=[compiler,'-std=c++23','-msse4.1','-mavx','@CMakeFiles/gh2_rex_gpu_smoke.dir/includes_CXX.rsp',
      '-I'+str(root/'tools/rexglue-sdk/thirdparty/imgui'),root/'tests/controller_mapping.cpp',
      root/'tools/rexglue-sdk/out/win-amd64/librexruntime.dll.a','-o',binary]
p=subprocess.run(args,cwd=root/'build/rexglue',capture_output=True,text=True)
if p.returncode:raise RuntimeError(p.stdout+p.stderr)
env=dict(os.environ);env['PATH']=str(root/'tools/rexglue-sdk/out/win-amd64')+os.pathsep+env['PATH']
results=[]
for args in [[binary],[binary,root/'build/controller_profile_test.toml']]:
    p=subprocess.run(args,env=env,capture_output=True,text=True,timeout=20)
    results.append(dict(exit_code=p.returncode,stdout=p.stdout,stderr=p.stderr))
    if p.returncode:raise RuntimeError(p.stdout+p.stderr)
report=dict(result='pass',tests=results,scope='Mapping logic, raw guest state, device routing and saved profile reload; hands-on gameplay test remains required.')
Path('docs/controller_mapping_validation.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report,indent=2))
