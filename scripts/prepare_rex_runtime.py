"""Prepare an isolated SDK-context build of the existing GH2 translations.

Use the SDK's actual C++ context type, never cast the diagnostic runtime's ABI.
Keep the diagnostic generated sources untouched for regression comparison.
"""
from pathlib import Path
import re

source = Path('generated')
out = Path('build/rex-generated')
out.mkdir(parents=True, exist_ok=True)
def write(name, text):
    path = out / name
    if not path.exists() or path.read_text() != text:
        path.write_text(text)

context = (source / 'ppc_context.h').read_text()
prefix = context[:context.index('typedef void PPCFunc')]
prefix = prefix.replace('#include "ppc_config.h"', '#include "ppc_config.h"\n#include <rex/ppc/context.h>')
# On Windows the pinned SDK emulates E-range physical-page skew with a host
# pointer offset. Wrap all base+address expressions, including inline VMX
# loads/stores, rather than changing scalar memory macros alone.
prefix = prefix.replace('#define PPC_FUNC(x) void x(PPCContext& __restrict ctx, uint8_t* base)', '''struct GH2ReXMemoryBase {
    uint8_t* raw;
    operator uint8_t*() const { return raw; }
    template<class T> uint8_t* operator+(T value) const {
        const uint32_t address = uint32_t(value);
        return raw + uint64_t(address) + (address >= 0xE0000000U ? 0x1000ULL : 0ULL);
    }
};
#define PPC_FUNC(x) void x(PPCContext& __restrict ctx, uint8_t* gh2_raw_base)''')
prefix = prefix.replace('#define PPC_FUNC_PROLOGUE() __builtin_assume(((size_t)base & 0x1F) == 0)', '#define PPC_FUNC_PROLOGUE() GH2ReXMemoryBase base{gh2_raw_base}; __builtin_assume(((size_t)gh2_raw_base & 0x1F) == 0)')
prefix = re.sub(r'#define PPC_LOOKUP_FUNC[^\n]+', '#define PPC_LOOKUP_FUNC(x, y) rex::runtime::ResolveIndirectFunction(uint32_t(y))', prefix)
prefix = prefix.replace('#define PPC_CALL_INDIRECT_FUNC(x) (PPC_LOOKUP_FUNC(base, x))(ctx, base)',
                        '#define PPC_CALL_INDIRECT_FUNC(x) do { ctx.last_indirect_target = uint32_t(x); (PPC_LOOKUP_FUNC(base, ctx.last_indirect_target))(ctx, base); } while (0)')
rounding = context[context.index('#define PPC_ROUND_NEAREST'):context.index('struct PPCFPSCRRegister')]
tail = context[context.index('inline uint8_t VectorMaskL[]'):]
write('ppc_context.h', prefix + rounding + tail)
write('gh2_ppc_helpers.h', Path('runtime/gh2_ppc_helpers.h').read_text().replace('uint32_t& vscr', 'uint8_t& vscr'))
write('gh2_guitar_compat.h', Path('runtime/gh2_guitar_compat.h').read_text())
write('controller_mapping.h', Path('runtime/controller_mapping.h').read_text())
write('local_profile.h', Path('runtime/local_profile.h').read_text())
write('cheat_settings.h', Path('runtime/cheat_settings.h').read_text())
write('native_cheats.h', Path('runtime/native_cheats.h').read_text())
write('discord_presence_state.h',Path('runtime/discord_presence_state.h').read_text())
write('scene_settings.h',Path('runtime/scene_settings.h').read_text())
write('game_frame_stats.h', Path('runtime/game_frame_stats.h').read_text())
for path in source.glob('ppc_recomp.*.cpp'):
    translated=path.read_text().replace('ctx.vscr', 'ctx.vscr_sat')
    if path.name=='ppc_recomp.58.cpp':
        anchor='PPC_FUNC_IMPL(__imp__sub_822471D0) {\n\tPPC_FUNC_PROLOGUE();'
        if translated.count(anchor)!=1:raise RuntimeError('Supported native crowd draw signature changed')
        translated=translated.replace('#include "ppc_recomp_shared.h"','#include "ppc_recomp_shared.h"\n#include "scene_settings.h"',1)
        translated=translated.replace(anchor,anchor+'\n    if(gh2::scene::skip_crowd_draw())return;',1)
    if path.name in ('ppc_recomp.99.cpp','ppc_recomp.111.cpp'):
        translated=translated.replace('#include "ppc_recomp_shared.h"','#include "ppc_recomp_shared.h"\n#include "native_cheats.h"\n#include "discord_presence_state.h"',1)
        function='sub_823187D0' if path.name=='ppc_recomp.99.cpp' else 'sub_823559D8'
        begin=translated.index('PPC_FUNC_IMPL(__imp__'+function+') {')
        end=translated.index('\n}\n',begin)
        part=translated[begin:end]
        param='ctx.r3.u32 == gh2::cheats::VariableMap ? PPC_LOAD_U32(ctx.r4.u32) : 0' if function=='sub_823187D0' else 'ctx.r4.u32'
        part=part.replace('PPC_FUNC_PROLOGUE();','PPC_FUNC_PROLOGUE();\n    const uint32_t gh2_name='+param+';',1)
        hook='observe_variable(gh2_name,ctx.r3.u32,base)' if function=='sub_823187D0' else 'observe_symbol(gh2_name,PPC_LOAD_U32(ctx.r3.u32),base)'
        presence='    gh2::discord::observe_variable(gh2_name,ctx.r3.u32,base);\n' if function=='sub_823187D0' else ''
        part=part.replace('\treturn;','    gh2::cheats::'+hook+';\n'+presence+'\treturn;')
        translated=translated[:begin]+part+translated[end:]
    if path.name=='ppc_recomp.171.cpp':
        anchor='ctx.lr = 0x826804E4;\n\tsub_823178C0(ctx, base);'
        if translated.count(anchor)!=1:raise RuntimeError('GH2 Hyperspeed call-site signature changed')
        translated=translated.replace('#include "ppc_recomp_shared.h"','#include "ppc_recomp_shared.h"\n#include "cheat_settings.h"',1)
        translated=translated.replace(anchor,anchor+'\n\tctx.r3.u64 = gh2::cheats::apply_hyperspeed(ctx.r3.u64);',1)
        speed_anchor='temp.u32 = PPC_LOAD_U32(ctx.r11.u32 + 31836);\n\tctx.f0.f64 = double(temp.f32);'
        if translated.count(speed_anchor)!=1:raise RuntimeError('GH2 Hyperspeed multiplier signature changed')
        translated=translated.replace(speed_anchor,'ctx.f0.f64 = double(gh2::cheats::highway_multiplier());',1)
    if path.name=='ppc_recomp.103.cpp':
        # DataFunc `exit`, registered at 0x8232DE6C..0x8232DE9C in this image.
        # Preserve the returned integer DataNode ABI; request normal host close.
        anchor='PPC_FUNC_IMPL(__imp__sub_8232B7B0) {\n\tPPC_FUNC_PROLOGUE();'
        if translated.count(anchor)!=1:raise RuntimeError('GH2 script-exit signature changed')
        translated=translated.replace('#include "ppc_recomp_shared.h"',
            '#include "ppc_recomp_shared.h"\nextern void gh2_request_menu_quit();',1)
        translated=translated.replace(anchor,anchor+'''
    PPC_STORE_U32(ctx.r3.u32, 0);
    PPC_STORE_U32(ctx.r3.u32 + 4, 0);
    gh2_request_menu_quit();
    return;
''',1)
    write(path.name, translated)
for name in ['ppc_config.h', 'ppc_recomp_shared.h']:
    write(name, (source / name).read_text())
mapping = (source / 'ppc_func_mapping.cpp').read_text()
imports = re.findall(r'\{\s*(0x[0-9A-Fa-f]+),\s*(__imp__\w+)\s*\}', mapping)
mapping = re.sub(r'^.*\{\s*0x[0-9A-Fa-f]+,\s*__imp__\w+\s*\},\n', '', mapping, flags=re.M)
write('ppc_func_mapping.cpp', mapping)
adapters = ['#include "ppc_recomp_shared.h"', '#include "gh2_guitar_compat.h"', '#include "controller_mapping.h"', '#include "game_frame_stats.h"', '#include "native_cheats.h"', '#include "discord_presence_state.h"', '#include <rex/runtime.h>', '#include <rex/system/function_dispatcher.h>', '#include <rex/system/kernel_module.h>', '#include <rex/logging.h>', '#include <stdexcept>', '#include <atomic>', f'static PPCFunc* resolved[{len(imports)}]{{}};', f'static std::atomic_bool visited[{len(imports)}]{{}};']
for index, (address, name) in enumerate(imports):
    before = after = ''
    if name == '__imp__VdSwap':
        before = 'gh2::performance::record_output(); gh2::cheats::apply_native(base); gh2::discord::sample(base); gh2::performance::SwapScope gh2_swap_scope;'
    elif name == '__imp__XamInputGetCapabilities':
        before = 'const uint32_t caps_address = ctx.r5.u32; const unsigned player = ctx.r3.u32 & 0xFF;'
        after = '''gh2::controls::adapt_capabilities(caps_address ? base + caps_address : nullptr, player, ctx.r3.u32);
    if (gh2::normalize_guitar_capabilities(caps_address ? base + caps_address : nullptr, ctx.r3.u32)) {
        static std::atomic_bool logged{false};
        if (!logged.exchange(true)) REXSYS_INFO("GH2 guitar compatibility: native guitar subtype normalized to 7; raw input preserved");
    }'''
    elif name == '__imp__XamInputGetState':
        before = 'const uint32_t state_address=ctx.r5.u32; const unsigned player=ctx.r3.u32 & 0xFF;'
        after = 'gh2::controls::remap_guest_state(state_address ? base + state_address : nullptr,player,ctx.r3.u32);'
    elif name in ('__imp__XamUserGetSigninState','__imp__XamUserGetName','__imp__XamUserGetXUID'):
        before = 'ctx.r3.u64=gh2::profile::runtime_user(ctx.r3.u32);'
    elif name == '__imp__XamShowSigninUI':
        before = 'const unsigned changed_users=gh2::profile::sign_in();'
        after = '''rex::Runtime::instance()->kernel_state()->BroadcastNotification(0x0000000A,changed_users);
    REXSYS_INFO("GH2 local profile signed in on player {} (existing save identity retained)",gh2::profile::signed_in_player.load()+1);'''
    adapters.append(f'''PPC_FUNC({name}) {{
    PPC_FUNC_PROLOGUE();
    auto* function = resolved[{index}];
    if (!function) throw std::runtime_error("SDK did not resolve {name} at {address}");
    if (!visited[{index}].exchange(true)) REXSYS_DEBUG("GH2 import {name.removeprefix('__imp__')} LR={{:08X}}", ctx.lr);
    {before}
    function(ctx, base);
    {after}
}}''')
adapters.append('''void gh2_rex_register_imports(rex::Runtime& runtime) {
    auto resolve = [&](const char* name, uint32_t guest, size_t index, PPCFunc* fallback) {
        for (const auto& table : runtime.export_resolver()->tables()) {
            for (auto* entry : table.exports_by_name()) {
                if (!entry || entry->type != rex::runtime::Export::Type::kFunction || std::strcmp(entry->name, name)) continue;
                auto module = runtime.kernel_state()->GetKernelModule(table.module_name());
                if (!module) continue;
                auto thunk = module->GetProcAddressByOrdinal(entry->ordinal, guest);
                if (thunk) resolved[index] = runtime.function_dispatcher()->GetFunction(thunk);
                break;
            }
            if (resolved[index]) break;
        }
        if (!runtime.function_dispatcher()->SetFunction(guest, fallback))
            throw std::runtime_error("SDK import registration failed");
        if (!resolved[index]) REXSYS_WARN("GH2 import {} has no SDK implementation", name);
    };''')
for index, (address, name) in enumerate(imports):
    adapters.append(f'    resolve("{name.removeprefix("__imp__")}", {address}, {index}, &{name});')
adapters.append('}')
write('gh2_rex_imports.cpp', '\n'.join(adapters))
print(f'Prepared SDK-context translations and {len(imports)} resolved import adapters.')

