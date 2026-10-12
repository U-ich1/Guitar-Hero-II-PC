"""Isolated official SDK probe; never loads or modifies GH2."""
from pathlib import Path
import argparse,json,os,subprocess
root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser();parser.add_argument('--compile-only',action='store_true');parser.add_argument('--evaluate',action='store_true');args=parser.parse_args()
sdk=root/'tools/streamline-sdk-v2.14.1';compiler=root/'tools/llvm-mingw-20261006-ucrt-x86_64/bin/clang++.exe';binary=root/'build/dlss_capability_test.exe'
subprocess.run([str(compiler),'-std=c++20','-O2','-DNOMINMAX','-DWIN32_LEAN_AND_MEAN','-I'+str(sdk/'include'),str(root/'tests/dlss_capability.cpp'),'-ld3d12','-ldxgi','-o',str(binary)],check=True)
if not args.compile_only:
    env=dict(os.environ);env['PATH']=str(compiler.parent)+os.pathsep+env['PATH']
    result=subprocess.run([str(binary),str(sdk/'bin/x64'),str(root/'build/dlss-probe-logs')]+(['--evaluate'] if args.evaluate else []),env=env,cwd=root,capture_output=True,text=True,timeout=55)
    (root/'build/dlss-capability-stdout.txt').write_text(result.stdout)
    (root/'build/dlss-capability-stderr.txt').write_text(result.stderr)
    print(result.stdout);print(result.stderr)
    parsed=json.loads(result.stdout[result.stdout.index('{'):].strip())
    report=dict(result='pass' if result.returncode==0 else 'unavailable',probe=parsed,exit_code=result.returncode,
                scope='Official SDK initialization, adapter support, device registration and mode dimension queries. Optional GPU reconstruction uses a synthetic static flat-color scene and verifies readback; no GH2 reconstruction, motion quality or performance measured.')
    (root/'docs/dlss_capability_validation.json').write_text(json.dumps(report,indent=2))
