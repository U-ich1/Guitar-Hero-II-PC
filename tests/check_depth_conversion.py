from pathlib import Path
import argparse,json,os,subprocess
root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser();parser.add_argument('--compile-only',action='store_true');args=parser.parse_args()
compiler=root/'tools/llvm-mingw-20261006-ucrt-x86_64/bin/clang++.exe';binary=root/'build/depth_conversion_test.exe'
subprocess.run([str(compiler),'-std=c++20','-O2','-DNOMINMAX','-DWIN32_LEAN_AND_MEAN',str(root/'tests/depth_conversion.cpp'),'-ld3d12','-ldxgi','-ld3dcompiler','-o',str(binary)],check=True)
if not args.compile_only:
    env=dict(os.environ);env['PATH']=str(compiler.parent)+os.pathsep+env['PATH']
    result=subprocess.run([str(binary)],env=env,capture_output=True,text=True,timeout=45)
    report=dict(result='pass' if result.returncode==0 else 'failed',stdout=result.stdout,stderr=result.stderr,scope='Actual D3D12 depth conversion shader and CPU readback comparisons with synthetic depth resources. No GH2 layer ownership, renderer hook, motion field or reconstruction validated.')
    (root/'docs/depth_conversion_validation.json').write_text(json.dumps(report,indent=2));print(result.stdout);print(result.stderr)
    if result.returncode:raise RuntimeError('Depth conversion fixture failed')
