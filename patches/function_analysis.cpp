#include "function.h"
#include <disasm.h>
#include <byteswap.h>
#include <algorithm>
#include <cstring>
#include <unordered_set>

size_t Function::SearchBlock(size_t address) const {
    for (size_t i = 0; i < blocks.size(); ++i) {
        if (address >= base + blocks[i].base && address < base + blocks[i].base + blocks[i].size) return i;
    }
    return size_t(-1);
}

// Follow the reachable control-flow graph, including recovered switch edges.
// Every read and edge is bounded to the caller's section/function interval.
Function Function::Analyze(const void* code, size_t size, size_t base,
                           const IndirectBranchTargets* indirect) {
    Function fn{base, 0};
    const auto* bytes = static_cast<const uint8_t*>(code);
    std::vector<size_t> pending{base};
    std::unordered_set<size_t> visited;
    auto valid = [&](size_t address) {
        return address >= base && address - base < size && size - (address - base) >= 4 && (address - base) % 4 == 0;
    };
    auto enqueue = [&](size_t address) { if (valid(address)) pending.push_back(address); };
    while (!pending.empty()) {
        size_t address = pending.back();
        pending.pop_back();
        while (valid(address) && visited.insert(address).second) {
            uint32_t raw;
            memcpy(&raw, bytes + address - base, 4);
            const uint32_t instruction = ByteSwap(raw);
            if (instruction == 0) break;
            ppc_insn decoded;
            ppc::Disassemble(bytes + address - base, address, decoded);
            if (decoded.opcode == nullptr) break;
            fn.size = std::max(fn.size, address - base + 4);
            const auto op = PPC_OP(instruction);
            if (op == PPC_OP_B && !PPC_BL(instruction)) {
                const int64_t displacement = int32_t(PPC_BI(instruction));
                enqueue(PPC_BA(instruction) ? size_t(displacement) : size_t(int64_t(address) + displacement));
                break;
            }
            if (op == PPC_OP_BC && !PPC_BL(instruction)) {
                const int64_t displacement = int32_t(PPC_BD(instruction));
                enqueue(PPC_BA(instruction) ? size_t(displacement) : size_t(int64_t(address) + displacement));
                if ((PPC_BO(instruction) & 0x14) == 0x14) break;
            }
            if (op == PPC_OP_CTR && (PPC_XOP(instruction) == 16 || PPC_XOP(instruction) == 528) && !PPC_BL(instruction)) {
                if (PPC_XOP(instruction) == 528 && indirect) {
                    auto found = indirect->find(address);
                    if (found != indirect->end()) for (auto target : found->second) enqueue(target);
                }
                if ((PPC_BO(instruction) & 0x14) == 0x14) break;
            }
            address += 4;
        }
    }
    // A data/padding word still consumes space for sequential section scanning.
    if (fn.size == 0 && size >= 4) fn.size = 4;
    if (fn.size) fn.blocks.emplace_back(0, fn.size);
    return fn;
}
