"""Compile-check every emitted translation unit and audit unresolved branches."""
import concurrent.futures
import json
import os
import re
import subprocess
from pathlib import Path

compiler = Path('tools/llvm-mingw-20261006-ucrt-x86_64/bin/clang++.exe').resolve()
files = sorted(Path('generated').glob('*.cpp'))
destination = Path('build/syntax')
destination.mkdir(parents=True, exist_ok=True)
env = dict(os.environ)
env['PATH'] = str(compiler.parent) + os.pathsep + env['PATH']
def check(path):
    command = [str(compiler), '-std=c++20', '-msse4.1', '-mavx', '-fsyntax-only', '-ferror-limit=8', '-Igenerated', '-Itools/XenonRecomp/thirdparty/simde', str(path)]
    process = subprocess.run(command, capture_output=True, text=True, env=env)
    (destination / (path.name + '.log')).write_text(process.stdout + process.stderr)
    missing = re.findall(r'// ERROR ([0-9A-F]+)', path.read_text())
    return {'file': path.name, 'exit_code': process.returncode, 'unresolved_call_targets': missing}

results = []
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
    for i, result in enumerate(pool.map(check, files), 1):
        results.append(result)
        if i % 25 == 0 or result['exit_code']:
            print(f"Checked {i}/{len(files)}; {result['file']}: {'pass' if result['exit_code'] == 0 else 'FAIL'}", flush=True)
summary = {'checked_translation_units': len(results),
           'failed_translation_units': [r['file'] for r in results if r['exit_code']],
           'unresolved_direct_call_occurrences': sum(len(r['unresolved_call_targets']) for r in results),
           'unresolved_direct_call_targets': sorted({target for r in results for target in r['unresolved_call_targets']})}
Path('docs/compile_validation.json').write_text(json.dumps(summary, indent=2))
print(json.dumps(summary, indent=2), flush=True)
raise SystemExit(bool(summary['failed_translation_units'] or summary['unresolved_direct_call_occurrences']))
