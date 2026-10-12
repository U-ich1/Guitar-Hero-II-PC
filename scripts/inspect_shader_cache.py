"""Build the SDK's existing shader disassembler as an isolated offline utility."""
from pathlib import Path
import argparse,os,subprocess
root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser();parser.add_argument('--cache',type=Path,default=root/'build/windowed-state/cache/shaders/shareable/415607E7.xsh');parser.add_argument('--output',type=Path,default=root/'build/temporal-shaders');args=parser.parse_args()
compiler=root/'tools/llvm-mingw-20261006-ucrt-x86_64/bin/clang++.exe'
binary=root/'build/performance-stage/inspect_shader_cache.exe'
subprocess.run([str(compiler),'-std=c++23','-msse4.1','-mavx','-DNOMINMAX','-DWIN32_LEAN_AND_MEAN',
    '-I'+str(root/'tools/rexglue-sdk/thirdparty/xxHash'),
    '@CMakeFiles/gh2_rex_gpu_smoke.dir/includes_CXX.rsp',
    str(root/'scripts/inspect_shader_cache.cpp'),
    str(root/'build/rexglue/src/graphics/CMakeFiles/rexgpu-xenos.dir/pipeline/shader/shader.cpp.obj'),
    str(root/'build/rexglue/src/graphics/CMakeFiles/rexgpu-xenos.dir/pipeline/shader/translator.cpp.obj'),
    str(root/'build/rexglue/src/graphics/CMakeFiles/rexgpu-xenos.dir/pipeline/shader/translator_disasm.cpp.obj'),
    str(root/'build/rexglue/src/graphics/CMakeFiles/rexgpu-xenos.dir/format/ucode.cpp.obj'),
    str(root/'build/rexglue/src/graphics/CMakeFiles/rexgpu-xenos.dir/flags.cpp.obj'),
    str(root/'tools/rexglue-sdk/out/win-amd64/librexruntime.dll.a'),'-o',str(binary)],cwd=root/'build/rexglue',check=True)
env=dict(os.environ);env['PATH']=str(compiler.parent)+os.pathsep+env['PATH']
subprocess.run([str(binary),str(args.cache),str(args.output)],env=env,check=True,cwd=root)
