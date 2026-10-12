from pathlib import Path
import subprocess,os,json,sys
root=Path(__file__).resolve().parents[1];os.chdir(root)
compiler=root/'tools/llvm-mingw-20261006-ucrt-x86_64/bin/clang++.exe';binary=root/'build/directinput_test.exe'
args=[compiler,'-std=c++23','-msse4.1','-mavx','-DNOMINMAX','-DWIN32_LEAN_AND_MEAN',
      '@CMakeFiles/gh2_rex_gpu_smoke.dir/includes_CXX.rsp','-I'+str(root/'tools/rexglue-sdk/thirdparty/imgui'),
      root/'tests/directinput.cpp',root/'tools/rexglue-sdk/out/win-amd64/librexruntime.dll.a','-ldinput8','-ldxguid','-lole32','-luser32','-o',binary]
p=subprocess.run(args,cwd=root/'build/rexglue',capture_output=True,text=True)
if p.returncode:raise RuntimeError(p.stdout+p.stderr)
env=dict(os.environ);env['PATH']=str(root/'tools/rexglue-sdk/out/win-amd64')+os.pathsep+env['PATH'];results=[]
runs=[[binary],[binary,root/'build/directinput_profile_test.toml']]
if '--probe' in sys.argv:runs.append([binary,'--probe'])
for args in runs:
    p=subprocess.run(args,env=env,capture_output=True,text=True,timeout=20)
    results.append(dict(exit_code=p.returncode,stdout=p.stdout,stderr=p.stderr))
    if p.returncode:raise RuntimeError(p.stdout+p.stderr)
report=dict(result='pass',tests=results,scope='DirectInput conversion, persisted device/profile and optional physical enumeration. Gameplay on a physical DirectInput controller remains to be verified.')
Path('docs/directinput_validation.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
if '--probe' in sys.argv:Path('docs/controller_devices_validation.json').write_text(json.dumps(results[-1],indent=2))
