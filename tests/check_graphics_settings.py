from pathlib import Path
import os,subprocess,json,sys
root=Path(__file__).resolve().parents[1];os.chdir(root)
compiler=root/'tools/llvm-mingw-20261006-ucrt-x86_64/bin/clang++.exe'
binary=Path(os.environ.get('GH2_RUNTIME_TEST_DIR',root/'tools/rexglue-sdk/out/win-amd64'))/'gh2_graphics_test.exe'
args=[compiler,'-std=c++23','-msse4.1','-mavx','-DNOMINMAX','-DWIN32_LEAN_AND_MEAN',
      '@CMakeFiles/gh2_rex_gpu_smoke.dir/includes_CXX.rsp','-I'+str(root/'tools/rexglue-sdk/thirdparty/imgui'),
      root/'tests/graphics_settings.cpp',root/'tools/rexglue-sdk/out/win-amd64/librexruntime.dll.a','-o',binary]
p=subprocess.run(args,cwd=root/'build/rexglue',capture_output=True,text=True)
if p.returncode:raise RuntimeError(p.stdout+p.stderr)
env=dict(os.environ);env['PATH']=str(binary.parent)+os.pathsep+env['PATH'];results=[]
runs=[[binary,'--settings'],[binary,'--reload'],[binary,'--late-reload']]
if '--launcher' in sys.argv:runs.append([binary,'--launcher-config'])
if '--gpu' in sys.argv:runs.extend([[binary,'--gpu',str(scale)] for scale in (1,2,3)] + [[binary,'--startup-gpu','2']])
for args in runs:
    logfile=root/'build/graphics_runtime_test.log'
    before=len(logfile.read_text(errors='replace')) if logfile.exists() else 0
    p=subprocess.run(args,env=env,capture_output=True,text=True,timeout=30)
    item=dict(arguments=[str(a) for a in args[1:]],exit_code=p.returncode,stdout=p.stdout,stderr=p.stderr)
    if len(args)>2 and args[1] in ('--gpu','--startup-gpu'):
        expected='GH2 internal render scale initialized: '+args[2]+'x'+args[2]
        item['actual_render_scale_confirmed']=expected in logfile.read_text(errors='replace')[before:]
        if not item['actual_render_scale_confirmed']:raise RuntimeError('Requested scale was not confirmed by the renderer: '+str(args[2]))
    results.append(item)
    if p.returncode:raise RuntimeError(p.stdout+p.stderr)
report=dict(result='pass',tests=results,string_round_trip='Windows path, quotes, backslashes, newline, tab, carriage return and UTF-8',scope='Actual renderer setting registration/persistence, host presentation policy and optional D3D12 initialization. No full-game frame or timing benchmark.')
Path('docs/graphics_settings_validation.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
