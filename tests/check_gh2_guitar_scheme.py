"""Execute GH2's actual translated controller classifier with native guitar caps."""
from pathlib import Path
import subprocess
import json
import os
import sys

root = Path(__file__).resolve().parents[1]
os.chdir(root)
work = Path('build/guitar_scheme_tests')
work.mkdir(parents=True, exist_ok=True)
functions = {
    'ppc_recomp.68.cpp': ['sub_8227B3F0', 'sub_8227B220', 'sub_8227B2A8'],
    'ppc_recomp.132.cpp': ['sub_823B5970', 'sub_823B5978'],
    'ppc_recomp.129.cpp': ['__savegprlr_25', '__restgprlr_25'],
}
code = ['#include "ppc_recomp_shared.h"']
for file, names in functions.items():
    source = (Path('build/rex-generated') / file).read_text()
    for name in names:
        start = source.index(f'PPC_FUNC_IMPL(__imp__{name}) {{')
        end = source.index('\n}\n', start) + 3
        code.append(source[start:end].replace(f'PPC_FUNC_IMPL(__imp__{name})', f'PPC_FUNC({name})', 1))
(work / 'actual_poll.cpp').write_text('\n'.join(code))
harness = r'''
#include "ppc_recomp_shared.h"
#include "gh2_guitar_compat.h"
#include <windows.h>
#include <fstream>
#include <vector>
#include <iostream>
#include <stdexcept>
uint8_t native_subtype = 6;
bool fix_enabled = false;
uint16_t native_buttons;
PPC_FUNC(__imp__XamInputGetState) {
    PPC_FUNC_PROLOGUE();
    const auto pointer = ctx.r5.u32;
    std::memset(base + pointer, 0, 16);
    PPC_STORE_U16(pointer + 4, native_buttons);
    PPC_STORE_U8(pointer + 6, 229); // Observed idle guitar axis, not a green fret.
    ctx.r3.u64 = 0;
}
PPC_FUNC(__imp__XamInputGetCapabilities) {
    PPC_FUNC_PROLOGUE();
    auto* caps = base + ctx.r5.u32;
    std::memset(caps, 0, 20); caps[0] = 1; caps[1] = native_subtype;
    ctx.r3.u64 = 0;
    if (fix_enabled) gh2::normalize_guitar_capabilities(caps, ctx.r3.u32);
}
int main() {
    uint8_t* raw = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x100000000ULL, MEM_RESERVE, PAGE_NOACCESS));
    if (!raw) return 1;
    struct Guard { uint8_t* p; ~Guard() { VirtualFree(p, 0, MEM_RELEASE); } } guard{raw};
    try {
        if (!VirtualAlloc(raw + 0x82000000, 0x890000, MEM_COMMIT, PAGE_READWRITE) ||
            !VirtualAlloc(raw + 0x100000, 0x10000, MEM_COMMIT, PAGE_READWRITE) ||
            !VirtualAlloc(raw + 0x200000, 0x10000, MEM_COMMIT, PAGE_READWRITE))
            throw std::runtime_error("Memory commit failed");
        std::ifstream image("private/default.exe", std::ios::binary);
        image.read(reinterpret_cast<char*>(raw + 0x82000000), 0x890000);
        auto poll = [&] {
            PPCContext ctx{};
            ctx.fpscr.loadFromHost();
            ctx.r1.u64 = 0x10FF00;
            ctx.r3.u64 = 0;
            ctx.r4.u64 = 0x200000; ctx.r5.u64 = 0x200010;
            ctx.r6.u64 = 0x200020; ctx.r7.u64 = 0x200030;
            ctx.r8.u64 = 0x200040; ctx.r9.u64 = 0x200050;
            sub_8227B3F0(ctx, raw);
            return ctx.r3.u32;
        };
        if (poll() != 2) throw std::runtime_error("Native subtype 6 did not reproduce gamepad fallback");
        unsigned count = 0;
        for (uint8_t type : {6, 7, 11}) {
            native_subtype = type; fix_enabled = true;
            for (uint16_t buttons : {0, 0x1000, 0x2000, 0x8000, 0x4000, 0x0100, 0x0001, 0x0002, 0xB100}) {
                native_buttons = buttons;
                if (poll() != 5) throw std::runtime_error("GH2 did not select guitar scheme after compatibility fix");
                ++count;
            }
        }
        native_subtype = 1;
        if (poll() != 2) throw std::runtime_error("Standard gamepad was incorrectly classified as guitar");
        uint8_t caps[20]{1,6};
        if (gh2::normalize_guitar_capabilities(caps, 1) || caps[1] != 6)
            throw std::runtime_error("Failed capability query was modified");
        std::cout << "PASS: actual GH2 controller poll reproduces subtype-6 gamepad fallback; "
                  << count << " fixed guitar polls return scheme 5; standard gamepad stays scheme 2.\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
}
'''
(work / 'harness.cpp').write_text(harness)
compiler = root / 'tools/llvm-mingw-20261006-ucrt-x86_64/bin/clang++.exe'
optimized='--optimized' in sys.argv
args = [compiler, '-std=c++23', '-msse4.1', '-mavx', '-O2' if optimized else '-O0', '-fwrapv', '-fno-strict-aliasing', '-ffp-model=strict', '-Ibuild/rex-generated',
        '-Itools/rexglue-sdk/include', '-Ibuild/rexglue/include', '-Itools/rexglue-sdk/thirdparty/simde',
        '-Itools/rexglue-sdk/thirdparty/simde/simde', work / 'actual_poll.cpp', work / 'harness.cpp',
        '-o', work / 'poll.exe']
result = subprocess.run(args, capture_output=True, text=True)
if result.returncode: raise RuntimeError(result.stdout + result.stderr)
env = dict(os.environ)
env['PATH'] = str(compiler.parent) + os.pathsep + env['PATH']
result = subprocess.run([str((work / 'poll.exe').resolve())], env=env, capture_output=True, text=True)
(work / 'result.log').write_text(result.stdout + result.stderr)
report = dict(exit_code=result.returncode, stdout=result.stdout, stderr=result.stderr,
              scope="Actual translated GH2 controller poll and decoded game constants; full highway fret response still needs hardware verification.")
report['optimization']='O2, strict FP, wrapping signed arithmetic' if optimized else 'O0'
Path('docs/optimized_guitar_scheme_validation.json' if optimized else 'docs/guitar_scheme_validation.json').write_text(json.dumps(report, indent=2))
print(json.dumps(report, indent=2))
raise SystemExit(result.returncode)
