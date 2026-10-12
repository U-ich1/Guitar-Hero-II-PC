"""Execute the native crowd gate in isolation, never launch the guest game."""
from pathlib import Path
import re,subprocess,os,json,struct
root=Path(__file__).resolve().parents[1]
image=(root/'private/default.exe').read_bytes()
# Exact supported image: WorldCrowd's drawable table uses this draw routine.
assert struct.unpack_from('>I',image,0x2f110)[0]==0x822471D0
source=(root/'generated/ppc_recomp.58.cpp').read_text()
translated=(root/'build/rex-generated/ppc_recomp.58.cpp').read_text()
tag='PPC_FUNC_IMPL(__imp__sub_822471D0) {'
def extract(text):
    start=text.index(tag);return text[start:text.index('\n}\n',start)+3]
original=extract(source).replace('ctx.vscr','ctx.vscr_sat')
patched=extract(translated)
gate='\n    if(gh2::scene::skip_crowd_draw())return;'
assert patched.replace(gate,'')==original and patched.count(gate)==1
body=patched.replace('PPC_FUNC_IMPL(__imp__sub_822471D0)','PPC_FUNC(test_crowd)',1)
calls=set(re.findall(r'\b((?:sub_|__save|__rest)[A-Za-z0-9_]+)\(ctx, base\)',body))
stubs=[]
for name in sorted(calls):
    if name.startswith('__save')or name.startswith('__rest'):stubs.append(f'PPC_FUNC({name}){{++helpers;}}')
    else:stubs.append(f'PPC_FUNC({name}){{throw std::runtime_error("Unexpected crowd workload: {name}");}}')
fixture=r'''
#include "ppc_recomp_shared.h"
#include "scene_settings.h"
#include <windows.h>
#include <iostream>
#include <cstring>
#include <stdexcept>
static int helpers=0;
'''+ '\n'.join(stubs)+'\n'+body+r'''
int main(){try{
    auto* raw=static_cast<uint8_t*>(VirtualAlloc(nullptr,1024*1024,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!raw)throw std::runtime_error("Fixture allocation failed");
    PPCContext ctx{};ctx.r1.u64=0x80000;ctx.r3.u64=0x20000;
    const auto before=ctx;
    if(!rex::cvar::SetFlagByName("gh2_no_audience","true"))throw std::runtime_error("Toggle rejected");
    test_crowd(ctx,raw);
    if(std::memcmp(&ctx,&before,sizeof(ctx))||helpers||gh2::scene::crowd_draws_skipped!=1)throw std::runtime_error("Hidden draw changed guest context or ran workload");
    if(rex::cvar::SetFlagByName("gh2_no_audience","garbage"))throw std::runtime_error("Invalid toggle accepted");
    if(!rex::cvar::SetFlagByName("gh2_no_audience","false"))throw std::runtime_error("Restore toggle rejected");
    test_crowd(ctx,raw);
    if(helpers!=4||ctx.r1.u64!=before.r1.u64||gh2::scene::crowd_draws_skipped!=1)throw std::runtime_error("Native empty-crowd branch not restored");
    VirtualFree(raw,0,MEM_RELEASE);
    std::cout<<"PASS: supported WorldCrowd drawable-table identity, unchanged native body when disabled, early render skip preserves context and runs no crowd work, live restore and flag validation. No game launched.\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
'''
work=root/'build/scene-test';work.mkdir(exist_ok=True);(work/'scene.cpp').write_text(fixture)
compiler=root/'tools/llvm-mingw-20261006-ucrt-x86_64/bin/clang++.exe';binary=root/'build/performance-stage/scene_test.exe'
command=[str(compiler),'-std=c++23','-O2','-msse4.1','-mavx','-DNOMINMAX','-DWIN32_LEAN_AND_MEAN','@CMakeFiles/gh2_pc.dir/includes_CXX.rsp',str(work/'scene.cpp'),'@CMakeFiles/gh2_pc.dir/linkLibs.rsp','-o',str(binary)]
compile=subprocess.run(command,cwd=root/'build/rexglue',capture_output=True,text=True)
if compile.returncode:raise RuntimeError((compile.stdout+compile.stderr)[-2000:])
env=dict(os.environ);env['PATH']=str(binary.parent)+os.pathsep+str(compiler.parent)+os.pathsep+env['PATH']
result=subprocess.run([str(binary)],env=env,capture_output=True,text=True,timeout=20)
print(result.stdout,result.stderr)
(root/'docs/scene_settings_validation.json').write_text(json.dumps(dict(result='pass' if result.returncode==0 else 'failed',stdout=result.stdout,limitations='Synthetic native render gate and empty-crowd fallback only. Visible audience removal, shadow behavior and FPS improvement need gameplay observation.'),indent=2))
if result.returncode:raise RuntimeError('Crowd gate fixture failed')
