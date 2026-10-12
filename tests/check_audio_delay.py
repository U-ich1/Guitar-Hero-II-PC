from pathlib import Path
import subprocess
import os
import json
root = Path(__file__).resolve().parents[1]
os.chdir(root)
compiler = root / 'tools/llvm-mingw-20261006-ucrt-x86_64/bin/clang++.exe'
binary = root / 'build/audio_delay_test.exe'
args = [compiler, '-std=c++23', '-msse4.1', '-mavx',
        '@CMakeFiles/gh2_rex_gpu_smoke.dir/includes_CXX.rsp', root / 'tests/audio_delay.cpp',
        root / 'tools/rexglue-sdk/out/win-amd64/librexruntime.dll.a', '-o', binary]
result = subprocess.run(args, cwd=root / 'build/rexglue', capture_output=True, text=True)
if result.returncode: raise RuntimeError(result.stdout + result.stderr)
env = dict(os.environ)
env['PATH'] = str(root / 'tools/rexglue-sdk/out/win-amd64') + os.pathsep + env['PATH']
results = []
for args in [[binary], [binary, root / 'build/audio_offset_test.toml']]:
    result = subprocess.run(args, env=env, capture_output=True, text=True, timeout=20)
    results.append(dict(exit_code=result.returncode, stdout=result.stdout, stderr=result.stderr))
    if result.returncode: raise RuntimeError(result.stdout + result.stderr)
report = dict(result='pass', tests=results,
              scope='Sample-exact added delay and persistence; physical audio/display latency and note judgment are not measured.')
Path('docs/audio_offset_validation.json').write_text(json.dumps(report, indent=2))
print(json.dumps(report, indent=2))
