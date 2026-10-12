"""Execute real emitted instruction functions against an independent guest-byte oracle."""
import json
import math
import os
import random
import struct
import sys
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parents[1]
os.chdir(root)
rex_context = '--rex-context' in sys.argv
optimized='--optimized' in sys.argv
work = Path('build/optimized_instruction_tests' if optimized else 'build/rex_instruction_tests' if rex_context else 'build/instruction_tests')
objects = work / 'objects'
emitted = work / 'emitted'
objects.mkdir(parents=True, exist_ok=True)
emitted.mkdir(parents=True, exist_ok=True)
bin_dir = Path('tools/llvm-mingw-20261006-ucrt-x86_64/bin').resolve()
env = dict(os.environ)
env['PATH'] = str(bin_dir) + os.pathsep + env['PATH']
def run(args):
    p = subprocess.run([str(x) for x in args], env=env, capture_output=True, text=True)
    if p.returncode:
        raise RuntimeError(p.stdout + p.stderr)
    return p

binary = ['vslh', 'vsrh', 'vsrah', 'vsrab', 'vrlh', 'vsubshs', 'vaddsbs', 'vaddsws', 'vmaxsh', 'vminsh', 'vavguh', 'vandc', 'vsububm', 'vpkswss', 'vpkswus', 'vpkshss', 'vpkuhus', 'vcmpequh', 'vcmpequh.', 'vcmpgtsh', 'vcmpgtsh.', 'vcmpgtsw.', 'vcmpgtuh.']
fixtures = [(op, d, f'{op} {d},4,5') for op in binary for d in (3, 4, 5)]
fixtures += [('vspltish', 3, f'vspltish 3,{n}') for n in (-16, -1, 0, 15)]
fixtures += [('vctuxs', d, f'vctuxs {d},4,{n}') for n in (0, 2, 31) for d in (3, 4)]
fixtures += [(op, 3, f'{op} 3,4,5') for op in ('eqv', 'eqv.', 'mulhd', 'mulhd.', 'lhbrx')]
fixtures += [(op, 3, f'{op} 6,7,8') for op in ('cror', 'crorc')]
# Native VMX128 encoding, VD=3, VB=60, scale=2; the source index uses split bits.
fixtures += [('vcfpuxws128', 3, '.long 0x1862E271')]
assembly = '.text\n' + ''.join(f'.global case_{i}\ncase_{i}:\n    {asm}\n    blr\n' for i, (_, _, asm) in enumerate(fixtures))
(work / 'instructions.s').write_text(assembly)
run(['C:/devkitPro/devkitPPC/bin/powerpc-eabi-as.exe', '-mppc64', '-maltivec', work / 'instructions.s', '-o', objects / 'instructions.o'])
translation = run(['build/xenon/XenonRecomp/XenonRecomp.exe', objects, emitted])
if 'unimplemented instructions' in translation.stdout or 'Unrecognized instruction' in translation.stdout:
    raise AssertionError(translation.stdout)
code = (emitted / 'instructions.cpp').read_text()
if '// vcfpuxws128 v3,v60,2' not in code:
    raise AssertionError('VMX128 fixture encoding did not decode as intended')
declarations = '\n'.join(f'PPC_FUNC(instructions_{i * 8:X});' for i in range(len(fixtures)))
functions = ','.join(f'instructions_{i * 8:X}' for i in range(len(fixtures)))
destinations = ','.join(str(d) for _, d, _ in fixtures)
harness = '''#define PPC_CONFIG_H_INCLUDED
#include <ppc_context.h>
#include <iostream>
#include <iomanip>
DECLARATIONS
int main() {
    PPCFunc* functions[] = {FUNCTIONS};
    unsigned destinations[] = {DESTINATIONS};
    alignas(32) uint8_t memory[64]{};
    memory[12] = 0x12; memory[13] = 0x34;
    unsigned index, initialVscr, crBits, n;
    uint64_t a, b;
    while (std::cin >> std::dec >> index >> std::hex >> a >> b >> initialVscr >> crBits) {
        if (index >= std::size(functions)) return 2;
        PPCContext ctx{};
        ctx.fpscr.loadFromHost();
        ctx.r3.u64 = 0; ctx.r4.u64 = a; ctx.r5.u64 = b; ctx.vscr = initialVscr;
        PPCCRRegister* cr[] = {&ctx.cr0,&ctx.cr1,&ctx.cr2,&ctx.cr3,&ctx.cr4,&ctx.cr5,&ctx.cr6,&ctx.cr7};
        for (unsigned i = 0; i < 32; ++i) {
            auto& f = *cr[i/4];
            uint8_t* bits[] = {&f.lt,&f.gt,&f.eq,&f.so};
            *bits[i%4] = (crBits >> (31-i)) & 1;
        }
        for (auto& byte : ctx.v4.u8) { std::cin >> n; byte = n; }
        for (auto& byte : ctx.v5.u8) { std::cin >> n; byte = n; }
        ctx.v60 = ctx.v4;
        functions[index](ctx, memory);
        const PPCVRegister* v = destinations[index] == 3 ? &ctx.v3 : destinations[index] == 4 ? &ctx.v4 : &ctx.v5;
        std::cout << std::hex;
        for (auto byte : v->u8) std::cout << unsigned(byte) << ' ';
        uint32_t result = 0;
        for (auto* f : cr) result = (result << 4) | (f->lt << 3) | (f->gt << 2) | (f->eq << 1) | f->so;
        std::cout << ctx.r3.u64 << ' ' << result << ' ' << ctx.vscr << '\\n';
    }
}
'''.replace('DECLARATIONS', declarations).replace('FUNCTIONS', functions).replace('DESTINATIONS', destinations)
(work / 'harness.cpp').write_text(harness)
includes = ['-Itools/XenonRecomp/XenonUtils', '-Itools/XenonRecomp/thirdparty/simde', '-Iruntime']
if rex_context:
    (emitted / 'instructions.cpp').write_text(code.replace('ctx.vscr', 'ctx.vscr_sat'))
    harness = harness.replace('ctx.vscr =', 'ctx.vscr_sat =').replace("<< ctx.vscr <<", "<< unsigned(ctx.vscr_sat) <<")
    (work / 'harness.cpp').write_text(harness)
    includes = ['-Ibuild/rex-generated', '-Itools/rexglue-sdk/include', '-Ibuild/rexglue/include', '-Itools/rexglue-sdk/thirdparty/simde', '-Itools/rexglue-sdk/thirdparty/simde/simde']
run([bin_dir / 'clang++.exe', '-std=c++23' if rex_context else '-std=c++20', '-O2' if optimized else '-O1', '-fwrapv', '-fno-strict-aliasing', '-ffp-model=strict', '-msse4.1', '-mavx', *includes, emitted / 'instructions.cpp', work / 'harness.cpp', '-o', work / 'instructions.exe'])

rng = random.Random(0x415607E7)
def unpack(data, size, signed=False):
    # Decode guest lanes from guest-order bytes; no recompiler helpers are used.
    data = data[::-1]
    return [int.from_bytes(data[i:i+size], 'big', signed=signed) for i in range(0, 16, size)]
def packed(values, size):
    mask = (1 << (size * 8)) - 1
    return b''.join((v & mask).to_bytes(size, 'big') for v in values)[::-1]
cases, expectations, labels = [], [], []
mask64 = (1 << 64) - 1
for index, (operation, destination, asm) in enumerate(fixtures):
    for trial in range(100):
        a = bytes(rng.getrandbits(8) for _ in range(16))
        b = bytes(rng.getrandbits(8) for _ in range(16))
        # Include exact zeros/all ones plus mixed lane saturation boundaries.
        if trial < 4:
            a = bytes([0, 255, 127, 128][trial:trial+1] * 16)
            b = bytes([255, 0, 128, 127][trial:trial+1] * 16)
        g4, g5 = rng.getrandbits(64), rng.getrandbits(64)
        if trial < 8:
            edge = [0, 1, 2**63-1, 2**63, 2**64-1, 2**64-2, 0xFFFFFFFF, 0x80000000]
            g4, g5 = edge[trial], edge[7-trial]
        cr = rng.getrandbits(32)
        initial_vscr = trial % 2
        out, g3, updated_cr, vscr = bytes(16), 0, cr, initial_vscr
        op = operation.rstrip('.')
        values = None
        if op.startswith('vspltish'):
            values, size = [int(asm.rsplit(',', 1)[1])] * 8, 2
        elif op in ('vctuxs', 'vcfpuxws128'):
            floats = [float('nan'), float('inf'), -1.0, 4294967296.0] if trial == 0 else [0.0, 1.75, 2147483648.0, 4294967040.0] if trial == 1 else [rng.uniform(-1e9, 5e9) for _ in range(4)]
            a = struct.pack('>4f', *floats)[::-1]
            source = struct.unpack('>4f', a[::-1])
            scale = 2 if op == 'vcfpuxws128' else int(asm.rsplit(',', 1)[1])
            values = []
            for x in source:
                try: x = struct.unpack('>f', struct.pack('>f', math.ldexp(x, scale)))[0]
                except OverflowError: x = math.copysign(math.inf, x)
                if math.isnan(x): values.append(0)
                elif x < 0: values.append(0); vscr |= 1
                elif x >= 2**32: values.append(2**32 - 1); vscr |= 1
                else: values.append(int(x))
            size = 4
        elif op in ('eqv', 'mulhd', 'lhbrx'):
            if op == 'eqv': g3 = ~(g4 ^ g5) & mask64
            elif op == 'mulhd':
                signed = lambda x: x - 2**64 if x & 2**63 else x
                g3 = (signed(g4) * signed(g5) >> 64) & mask64
            else: g4, g5, g3 = 4, 8, 0x3412
            if operation.endswith('.'):
                s = g3 - 2**64 if g3 & 2**63 else g3
                field = 8 if s < 0 else 4 if s > 0 else 2
                updated_cr = (cr & 0x0FFFFFFF) | (field << 28)
        elif op in ('cror', 'crorc'):
            get = lambda n: (cr >> (31-n)) & 1
            bit = get(7) | (get(8) ^ (op == 'crorc'))
            updated_cr = (cr & ~(1 << 25)) | (bit << 25)
        elif op == 'vandc': out = bytes(x & (~y & 255) for x, y in zip(a, b))
        elif op in ('vpkswss', 'vpkswus', 'vpkshss', 'vpkuhus'):
            size = 2 if op.startswith('vpksw') else 1
            inp = size * 2
            signed_in = op != 'vpkuhus'
            signed_out = op.endswith('ss')
            lo = -(1 << (size*8-1)) if signed_out else 0
            hi = (1 << (size*8-1)) - 1 if signed_out else (1 << (size*8)) - 1
            values = []
            for x in unpack(a, inp, signed_in) + unpack(b, inp, signed_in):
                values.append(max(lo, min(hi, x)))
                if x < lo or x > hi: vscr |= 1
        else:
            size = 1 if op in ('vsrab', 'vaddsbs', 'vsububm') else 4 if op in ('vaddsws', 'vcmpgtsw') else 2
            signed = op in ('vsrah', 'vsrab', 'vsubshs', 'vaddsbs', 'vaddsws', 'vmaxsh', 'vminsh', 'vcmpgtsh', 'vcmpgtsw')
            aa, bb = unpack(a, size, signed), unpack(b, size, signed)
            bits, mask = size*8, (1 << (size*8)) - 1
            values = []
            for x, y in zip(aa, bb):
                n = y & (bits-1)
                if op == 'vslh': z = x << n
                elif op in ('vsrh', 'vsrah', 'vsrab'): z = x >> n
                elif op == 'vrlh': z = ((x << n) | (x >> ((bits-n) % bits))) & mask
                elif op == 'vmaxsh': z = max(x, y)
                elif op == 'vminsh': z = min(x, y)
                elif op == 'vavguh': z = (x+y+1)//2
                elif op == 'vcmpequh': z = mask if x == y else 0
                elif op.startswith('vcmpgt'): z = mask if x > y else 0
                elif op == 'vsububm': z = x-y
                else:
                    z = x-y if op == 'vsubshs' else x+y
                    lo, hi = -(1 << (bits-1)), (1 << (bits-1))-1
                    if z < lo or z > hi: vscr |= 1
                    z = max(lo, min(hi, z))
                values.append(z)
            if operation.endswith('.'):
                field = 8 if all(values) else 2 if not any(values) else 0
                updated_cr = (cr & ~0xF0) | (field << 4)
        if values is not None: out = packed(values, size)
        cases.append(f'{index} {g4:x} {g5:x} {initial_vscr:x} {cr:x} ' + ' '.join(f'{x:x}' for x in a+b))
        expectations.append(list(out) + [g3, updated_cr, vscr])
        labels.append(f'{asm}, trial {trial}')
process = subprocess.run([str((work / 'instructions.exe').resolve())], input='\n'.join(cases)+'\n', capture_output=True, text=True, env=env)
if process.returncode: raise RuntimeError(process.stderr or f'Native fixture crashed: {process.returncode}')
lines = process.stdout.splitlines()
if len(lines) != len(expectations): raise AssertionError(f'Expected {len(expectations)} results, got {len(lines)}')
for line, expected, label in zip(lines, expectations, labels):
    actual = [int(x, 16) for x in line.split()]
    if actual != expected:
        raise AssertionError(f'{label}: expected {expected}, actual {actual}')
report = {'instruction_fixtures': len(fixtures), 'executed_cases': len(cases), 'result': 'pass', 'seed': '415607E7', 'scope': 'Actual emitted instruction functions; guest-byte oracle; destination aliasing; CR updates; sticky saturation. No full-game runtime validation.'}
report['context'] = 'ReXGlue SDK' if rex_context else 'diagnostic'
report['optimization']='O2, strict FP, wrapping signed arithmetic' if optimized else 'O1'
Path('docs/optimized_instruction_validation.json' if optimized else 'docs/rex_instruction_validation.json' if rex_context else 'docs/instruction_validation.json').write_text(json.dumps(report, indent=2))
print(json.dumps(report, indent=2))
