from pathlib import Path
import subprocess,os,json
root=Path(__file__).resolve().parents[1]
compiler=root/'tools/llvm-mingw-20261006-ucrt-x86_64/bin/clang++.exe'
binary=root/'build/performance-stage/discord_presence_test.exe'
args=[str(compiler),'-std=c++23','-O2','-DNOMINMAX','-DWIN32_LEAN_AND_MEAN','-DSPDLOG_COMPILED_LIB','-DSPDLOG_FMT_EXTERNAL',
    '@CMakeFiles/gh2_pc.dir/includes_CXX.rsp',str(root/'tests/discord_presence.cpp'),'@CMakeFiles/gh2_pc.dir/linkLibs.rsp','-o',str(binary)]
compiled=subprocess.run(args,cwd=root/'build/rexglue',capture_output=True,text=True)
if compiled.returncode:raise RuntimeError((compiled.stdout+compiled.stderr)[-3000:])
env=dict(os.environ);env['PATH']=str(binary.parent)+os.pathsep+str(compiler.parent)+os.pathsep+env['PATH']
result=subprocess.run([str(binary)],cwd=root,env=env,capture_output=True,text=True,timeout=30)
print(result.stdout,result.stderr)
(root/'docs/discord_presence_validation.json').write_text(json.dumps(dict(result='pass' if result.returncode==0 else 'failed',stdout=result.stdout,stderr=result.stderr,scope='Private mock IPC server and synthetic guest state, no live Discord activity or game execution.'),indent=2))
if result.returncode:raise RuntimeError('Discord fixture failed')
