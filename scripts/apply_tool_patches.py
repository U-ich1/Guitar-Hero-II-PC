"""Install project-owned extensions into the pinned local XenonRecomp checkout."""
from pathlib import Path
import shutil

root = Path('tools/XenonRecomp')
def replace(path, old, new):
    path = root / path
    text = path.read_text(encoding='utf-8')
    if new in text:
        return
    if old not in text:
        raise ValueError(f'Pinned source pattern missing: {path}')
    path.write_text(text.replace(old, new, 1), encoding='utf-8')

shutil.copyfile('patches/gh2_instruction_cases.inl', root / 'XenonRecomp/gh2_instruction_cases.inl')
replace('XenonRecomp/recompiler.cpp', '    switch (id)\n    {', '    switch (id)\n    {\n#include "gh2_instruction_cases.inl"')
replace('XenonRecomp/recompiler.cpp', '    case PPC_INST_VANDC128:', '    case PPC_INST_VANDC:\n    case PPC_INST_VANDC128:')
replace('XenonRecomp/recompiler.cpp', 'println("\\t{}.u64 = __rdtsc();", r(insn.operands[0]));', 'println("\\t{}.u64 = gh2_read_timebase();", r(insn.operands[0]));')
replace('XenonRecomp/recompiler.cpp', '''        println("\\t{}.u64 = (__builtin_rotateleft32({}.u32, {}) & 0x{:X}) | ({}.u64 & 0x{:X});", r(insn.operands[0]), r(insn.operands[1]), insn.operands[2], mask, r(insn.operands[0]), ~mask);
        break;''', '''        println("\\t{}.u64 = (__builtin_rotateleft32({}.u32, {}) & 0x{:X}) | ({}.u64 & 0x{:X});", r(insn.operands[0]), r(insn.operands[1]), insn.operands[2], mask, r(insn.operands[0]), ~mask);
        if (strchr(insn.opcode->name, '.'))
            println("\\t{}.compare<int32_t>({}.s32, 0, {});", cr(0), r(insn.operands[0]), xer());
        break;''')
replace('XenonRecomp/recompiler.cpp', '''    case PPC_INST_VCMPGTUH:
        println("\\tsimde_mm_store_si128((simde__m128i*){}.u8, simde_mm_cmpgt_epu16(simde_mm_load_si128((simde__m128i*){}.u16), simde_mm_load_si128((simde__m128i*){}.u16)));", v(insn.operands[0]), v(insn.operands[1]), v(insn.operands[2]));
        break;''', '''    case PPC_INST_VCMPGTUH:
        println("\\tsimde_mm_store_si128((simde__m128i*){}.u8, simde_mm_cmpgt_epu16(simde_mm_load_si128((simde__m128i*){}.u16), simde_mm_load_si128((simde__m128i*){}.u16)));", v(insn.operands[0]), v(insn.operands[1]), v(insn.operands[2]));
        if (strchr(insn.opcode->name, '.'))
            println("\\t{}.setFromMask(simde_mm_load_si128((simde__m128i*){}.u8), 0xFFFF);", cr(6), v(insn.operands[0]));
        break;''')
replace('XenonUtils/ppc_context.h', 'struct alignas(0x40) PPCContext\n{', 'struct alignas(0x40) PPCContext\n{\n    uint32_t vscr{}; // sticky vector saturation flag')
replace('XenonUtils/ppc_context.h', '#define PPC_FUNC_PROLOGUE() __builtin_assume(((size_t)base & 0x1F) == 0)', '''#ifdef GH2_TRACE_ENTRY
struct PPCContext;
void gh2_trace_entry(PPCContext&, const char*);
#define PPC_FUNC_PROLOGUE() __builtin_assume(((size_t)base & 0x1F) == 0); gh2_trace_entry(ctx, __func__)
#else
#define PPC_FUNC_PROLOGUE() __builtin_assume(((size_t)base & 0x1F) == 0)
#endif''')
path = root / 'XenonUtils/ppc_context.h'
text = path.read_text(encoding='utf-8')
if '#include "gh2_ppc_helpers.h"' not in text:
    index = text.rfind('#endif')
    path.write_text(text[:index] + '#include "gh2_ppc_helpers.h"\n\n' + text[index:], encoding='utf-8')
replace('XenonUtils/ppc_context.h', '#include "gh2_ppc_helpers.h"', '#include "gh2_ppc_helpers.h"\nuint64_t gh2_read_timebase();')
replace('XenonAnalyse/function.h', '#include <vector>', '#include <vector>\n#include <unordered_map>\nusing IndirectBranchTargets = std::unordered_map<size_t, std::vector<size_t>>;')
replace('XenonAnalyse/function.h', 'static Function Analyze(const void* code, size_t size, size_t base);', 'static Function Analyze(const void* code, size_t size, size_t base, const IndirectBranchTargets* indirect = nullptr);')
shutil.copyfile('patches/function_analysis.cpp', root / 'XenonAnalyse/function.cpp')
marker = '    for (const auto& section : image.sections)\n    {\n        if (!(section.flags & SectionFlags_Code))'
extension = '''    IndirectBranchTargets indirect;
    for (const auto& [start, table] : config.switchTables) {
        for (size_t address = start; address < size_t(start) + 0x80; address += 4) {
            const auto* bytes = static_cast<const uint32_t*>(image.Find(address));
            if (!bytes) break;
            const uint32_t instruction = ByteSwap(*bytes);
            if (instruction == 0x4E800420) {
                indirect.emplace(address, std::vector<size_t>(table.labels.begin(), table.labels.end()));
                break;
            }
        }
    }
    std::vector<size_t> knownStarts;
    for (const auto& symbol : image.symbols) if (symbol.type == Symbol_Function) knownStarts.push_back(symbol.address);
    std::sort(knownStarts.begin(), knownStarts.end());
    auto analyzeFunction = [&](const void* code, size_t size, size_t address) {
        auto next = std::upper_bound(knownStarts.begin(), knownStarts.end(), address);
        if (next != knownStarts.end()) size = std::min(size, *next - address);
        return Function::Analyze(code, size, address, &indirect);
    };

'''
replace('XenonRecomp/recompiler.cpp', marker, extension + marker)
replace('XenonRecomp/recompiler.cpp', 'Function::Analyze(data, section.base + section.size - address, address)', 'analyzeFunction(data, section.base + section.size - address, address)')
replace('XenonRecomp/recompiler.cpp', 'Function::Analyze(data, dataEnd - data, base)', 'analyzeFunction(data, dataEnd - data, base)')
print('Project-owned tool extensions applied.')
