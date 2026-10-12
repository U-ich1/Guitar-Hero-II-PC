"""Exercise the actual translated Hyperspeed branch without launching GH2."""
from pathlib import Path
import subprocess,os,json,struct
root=Path(__file__).resolve().parents[1];os.chdir(root)
work=root/'build/cheat_tests';work.mkdir(parents=True,exist_ok=True)
image=(root/'private/default.exe').read_bytes()
assert image[0x6804e0:0x6804e4]==bytes.fromhex('4bc973e1'), 'Unexpected Hyperspeed query instruction'
assert image[0xcaf48:0xcaf48+30]==b'cheat_enabled_hyper_speed_mode'
assert struct.unpack_from('>f',image,0x7c5c)[0]==1.5
source=(root/'build/rex-generated/ppc_recomp.99.cpp').read_text()
start=source.index('PPC_FUNC_IMPL(__imp__sub_823178C0) {');end=source.index('\n}\n',start)+3
equality=source[start:end].replace('PPC_FUNC_IMPL(__imp__sub_823178C0)','PPC_FUNC(sub_823178C0)',1)
def translated(file,name):
    text=(root/'build/rex-generated'/file).read_text();start=text.index('PPC_FUNC_IMPL(__imp__'+name+') {');end=text.index('\n}\n',start)+3
    return text[start:end].replace('PPC_FUNC_IMPL(__imp__'+name+')','PPC_FUNC('+name+')',1)
native_functions='\n'.join([translated('ppc_recomp.99.cpp',name) for name in ('sub_823187D0','sub_82318910')]+[translated('ppc_recomp.111.cpp','sub_823559D8')])
source=(root/'build/rex-generated/ppc_recomp.171.cpp').read_text()
start=source.index('loc_826804D4:');end=source.index('loc_82680510:',start)
branch=source[start:end]+'loc_82680510:\nreturn;\n'
assert branch.count('gh2::cheats::apply_hyperspeed')==1
code=r'''
#include "ppc_recomp_shared.h"
#include "cheat_settings.h"
#include "discord_presence_state.h"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
PPC_FUNC(sub_82120818){throw std::runtime_error("Unexpected temporary-node cleanup");}
PPC_FUNC(sub_823185B8){throw std::runtime_error("Fixture variable missing");}
PPC_FUNC(sub_823554B0){ctx.r3.u64=0;}
PPC_FUNC(sub_8235AB08){ctx.r3.u64=ctx.r4.u64;}
PPC_FUNC(sub_82355828){}
PPC_FUNC(__savegprlr_29){PPC_FUNC_PROLOGUE();PPC_STORE_U64(ctx.r1.u32-32,ctx.r29.u64);PPC_STORE_U64(ctx.r1.u32-24,ctx.r30.u64);PPC_STORE_U64(ctx.r1.u32-16,ctx.r31.u64);}
PPC_FUNC(__restgprlr_29){PPC_FUNC_PROLOGUE();ctx.r29.u64=PPC_LOAD_U64(ctx.r1.u32-32);ctx.r30.u64=PPC_LOAD_U64(ctx.r1.u32-24);ctx.r31.u64=PPC_LOAD_U64(ctx.r1.u32-16);}
'''+equality+'\n'+native_functions+'\nPPC_FUNC(highway_branch){PPC_FUNC_PROLOGUE();PPCRegister temp{};\n'+branch+'}\n'+r'''
int main(int argc,char**){
    try{
        using namespace gh2::cheats;
        if(argc>1){rex::cvar::LoadConfig("build/cheat_tests/settings.toml");require(hyperspeed.load()==1,"Cheat preference failed to reload");std::cout<<"PASS: Hyperspeed preference reloaded in a fresh process.\n";return 0;}
        require(!rex::cvar::SetFlagByName("gh2_hyperspeed","6")&&!rex::cvar::SetFlagByName("gh2_hyperspeed","-2")&&!rex::cvar::SetFlagByName("gh2_hyperspeed","bad"),"Invalid cheat setting accepted");
        uint8_t* raw=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x100000000ULL,MEM_RESERVE,PAGE_NOACCESS));require(raw,"Address space reservation failed");
        struct Guard{uint8_t* memory;~Guard(){VirtualFree(memory,0,MEM_RELEASE);}} guard{raw};
        require(VirtualAlloc(raw+0x82000000,0x100000,MEM_COMMIT,PAGE_READWRITE)&&VirtualAlloc(raw+0x100000,0x10000,MEM_COMMIT,PAGE_READWRITE),"Fixture commit failed");
        std::ifstream input("private/default.exe",std::ios::binary);input.read(reinterpret_cast<char*>(raw+0x82000000),0x100000);
        GH2ReXMemoryBase base{raw};
        const uint32_t map=uint32_t(int64_t(-2105540608)-6444);
        require(VirtualAlloc(raw+(map&~0xFFFFU),0x10000,MEM_COMMIT,PAGE_READWRITE),"Variable map fixture failed");
        constexpr const char* symbol_names[]={"air_guitar","eyeball_crowd_heads","monkey_crowd_heads"};
        for(unsigned i=0;i<3;++i){const uint32_t name=0x104000+i*128;strcpy(reinterpret_cast<char*>(raw+name),symbol_names[i]);PPCContext ctx{};ctx.r1.u64=0x10F000;ctx.r3.u64=0x107000;ctx.r4.u64=name;sub_823559D8(ctx,raw);require(symbols[i].load()==name&&PPC_LOAD_U32(ctx.r3.u32)==name,"Actual Symbol constructor hook ABI failed");}
        for(unsigned i=0;i<gh2::cheats::native.size();++i){auto& setting=gh2::cheats::native[i];const uint32_t name=0x104400+i*128,node=0x103000+i*64;strcpy(reinterpret_cast<char*>(raw+name),setting.variable);
            PPC_STORE_U32(map+4,node);PPC_STORE_U32(node+8,0);PPC_STORE_U32(node+12,0);PPC_STORE_U32(node+16,name);PPC_STORE_U32(node+28,0x555AAAFF);
            for(int mode=-1;mode<=setting.maximum;++mode){PPC_STORE_U32(node+20,77);PPC_STORE_U32(node+24,0);require(rex::cvar::SetFlagByName(setting.key,std::to_string(mode)),"Native cheat value rejected");
                PPCContext ctx{};ctx.r1.u64=0x10F000;ctx.r3.u64=name;ctx.r30.u64=1234;ctx.r31.u64=5678;sub_82318910(ctx,raw);
                require(ctx.r3.u32==node+20&&setting.node.load()==node+20,"Actual DataVariable hook returned wrong node");
                const bool symbol=(i==1||i==4)&&mode>0;
                const uint32_t expected=mode<0&&i!=0?77:symbol?symbols[i==1?0:unsigned(mode)].load():uint32_t(mode);
                require(PPC_LOAD_U32(node+20)==expected&&PPC_LOAD_U32(node+24)==(symbol?5U:0U),"Native cheat wrote incorrect value or type");
                require(PPC_LOAD_U32(node+28)==0x555AAAFF&&ctx.r30.u64==1234&&ctx.r31.u64==5678,"Native hook damaged adjacent node or registers");
            }
        }
        // DTB variable creation uses the shared map lookup directly, bypassing
        // DataVariable(Symbol). This is the path missed by the original hook.
        auto& unlock=gh2::cheats::native[0];constexpr uint32_t unlock_name=0x104400,unlock_node=0x103000,key_slot=0x108000;
        PPC_STORE_U32(map+4,unlock_node);PPC_STORE_U32(key_slot,unlock_name);
        PPC_STORE_U32(unlock_node+20,0);unlock.node.store(0);unlock.mode.store(1);
        PPCContext direct{};direct.r1.u64=0x10F000;direct.r3.u64=map;direct.r4.u64=key_slot;direct.r30.u64=1234;direct.r31.u64=5678;
        sub_823187D0(direct,raw);
        require(direct.r3.u32==unlock_node+20&&unlock.node.load()==unlock_node+20&&PPC_LOAD_U32(unlock_node+20)==1,"DTB direct variable lookup did not bind and apply Unlock All");
        unlock.mode.store(0);apply_native(base);require(PPC_LOAD_U32(unlock_node+20)==0,"Cached script variable did not follow live Off change");
        unlock.mode.store(1);apply_native(base);require(PPC_LOAD_U32(unlock_node+20)==1,"Cached script variable did not follow live On change");
        // A matching key in an unrelated map must not become a cheat target.
        constexpr uint32_t other_map=map+128;PPC_STORE_U32(other_map+4,unlock_node);unlock.node.store(0);PPC_STORE_U32(unlock_node+20,77);
        direct.r1.u64=0x10F000;direct.r3.u64=other_map;direct.r4.u64=key_slot;sub_823187D0(direct,raw);
        require(unlock.node.load()==0&&PPC_LOAD_U32(unlock_node+20)==77,"Unrelated variable map was overridden");
        for(int mode:{-1,0,1,2,3,4,5})for(int native:{0,1}){
            require(rex::cvar::SetFlagByName("gh2_hyperspeed",std::to_string(mode)),"Cheat setting rejected");
            PPCContext ctx{};ctx.fpscr.loadFromHost();ctx.r31.u64=0x100000;ctx.r3.u64=0x101000;ctx.r24.u64=1;ctx.r29.u64=0;
            PPC_STORE_U32(0x101000,uint32_t(native));PPC_STORE_U32(0x101004,0);
            ctx.f31.f64=2.0;ctx.f30.f64=7.0;ctx.f1.f64=9.0;
            highway_branch(ctx,raw);
            const bool active=mode<0?native!=0:mode!=0;
            require(ctx.f31.f64==(active?2.0*(mode<=1?1.5:1.5+0.25*(mode-1)):2.0),"Actual highway multiplier did not follow menu setting");
            require(ctx.f30.f64==7.0&&ctx.f1.f64==9.0,"Hook changed unrelated timing inputs");
            require(last_hyperspeed.load()==(mode<0?int(active):mode),"Applied-state reporting incorrect");
        }
        require(rex::cvar::SetFlagByName("gh2_hyperspeed","1"),"Save preference rejected");rex::cvar::SaveConfig("build/cheat_tests/settings.toml");
        std::cout<<"PASS: fourteen highway combinations at levels 1-5; native Symbol/DataVariable ABI, direct DTB lookup, live cached-variable toggles and unrelated-map isolation; unrelated timing inputs preserved.\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
'''
(work/'actual_hyperspeed.cpp').write_text(code)
compiler=root/'tools/llvm-mingw-20261006-ucrt-x86_64/bin/clang++.exe';binary=work/'cheats_test.exe'
args=[compiler,'-std=c++23','-msse4.1','-mavx','-DNOMINMAX','-DWIN32_LEAN_AND_MEAN','-I'+str(root/'build/rex-generated'),'@CMakeFiles/gh2_rex_gpu_smoke.dir/includes_CXX.rsp',work/'actual_hyperspeed.cpp',root/'tools/rexglue-sdk/out/win-amd64/librexruntime.dll.a','-o',binary]
result=subprocess.run(args,cwd=root/'build/rexglue',capture_output=True,text=True)
if result.returncode:raise RuntimeError(result.stdout+result.stderr)
env=dict(os.environ);env['PATH']=str(root/'tools/rexglue-sdk/out/win-amd64')+os.pathsep+env['PATH'];results=[]
for args in ([binary],[binary,'--reload']):
    result=subprocess.run(args,env=env,capture_output=True,text=True,timeout=20)
    results.append(dict(exit_code=result.returncode,stdout=result.stdout,stderr=result.stderr))
    if result.returncode:raise RuntimeError(result.stdout+result.stderr)
report=dict(result='pass',native_highway_multiplier=1.5,tests=results,scope='Actual translated Hyperspeed equality and multiplier branch, override modes and fresh-process persistence. Full-song visuals and judgment timing require a play test; no game launched or Documents preferences changed.')
(root/'docs/cheats_validation.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
