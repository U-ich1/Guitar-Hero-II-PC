// Diagnostic entrypoint runner: stops at the first unimplemented Xbox service.
#include "ppc_recomp_shared.h"
#include "kernel_runtime.h"
#include "guest_memory.h"
#include <filesystem>
#include <windows.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
#include <stdexcept>
#include <iomanip>

namespace {
thread_local const char* recentFunctions[32]{};
thread_local size_t traceCount = 0;
thread_local PPCContext* activeContext = nullptr;
uint8_t* activeMemory = nullptr;
LONG CALLBACK guestFault(EXCEPTION_POINTERS* exception) {
    const DWORD code = exception->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_BREAKPOINT) return EXCEPTION_CONTINUE_SEARCH;
    std::cerr << "Diagnostic stop: host exception 0x" << std::hex << code;
    if (code == EXCEPTION_ACCESS_VIOLATION && exception->ExceptionRecord->NumberParameters >= 2) {
        const auto address = exception->ExceptionRecord->ExceptionInformation[1];
        std::cerr << ", " << (exception->ExceptionRecord->ExceptionInformation[0] == 1 ? "write" : "read") << " at host 0x" << address;
        if (activeMemory && address >= uintptr_t(activeMemory) && address - uintptr_t(activeMemory) < PPC_MEMORY_SIZE)
            std::cerr << " (guest 0x" << address - uintptr_t(activeMemory) << ")";
    }
    if (activeContext) std::cerr << ", guest LR=0x" << activeContext->lr << ", r13=0x" << activeContext->r13.u64;
    std::cerr << "\nRecent native guest functions:\n";
    const size_t begin = traceCount > 32 ? traceCount - 32 : 0;
    for (size_t i = begin; i < traceCount; ++i) std::cerr << "  " << recentFunctions[i % 32] << '\n';
    std::cerr << std::flush;
    std::_Exit(3);
}
}

void gh2_trace_entry(PPCContext& context, const char* name) {
    gh2::kernel::checkpoint();
    activeContext = &context;
    recentFunctions[traceCount++ % 32] = name;
}

struct GuestMemory : GuestAddressSpace {
    ~GuestMemory() { gh2::kernel::shutdown(); }
};
int main(int argc, char** argv) {
    if (argc != 3 || std::string(argv[1]) != "--trace-entry") {
        std::cout << "GH2 diagnostic bootstrap. Usage: gh2_bootstrap --trace-entry private/default.exe\n";
        return 0;
    }
    try {
        std::ifstream file(argv[2], std::ios::binary);
        if (!file) throw std::runtime_error("Decoded memory image not found");
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
        if (bytes.size() < 0x1000 || bytes.size() > PPC_IMAGE_SIZE || bytes[0] != 'M' || bytes[1] != 'Z')
            throw std::runtime_error("Unexpected decoded image; re-run image inspection");
        GuestMemory memory;
        activeMemory = memory.base;
        AddVectoredExceptionHandler(1, guestFault);
        memory.commit(PPC_IMAGE_BASE, PPC_IMAGE_SIZE + PPC_CODE_SIZE * 2 + 16);
        memcpy(memory.base + PPC_IMAGE_BASE, bytes.data(), bytes.size());
        size_t mapped = 0;
        for (auto* mapping = PPCFuncMappings; mapping->host; ++mapping) {
            if (mapping->guest < PPC_CODE_BASE || mapping->guest >= PPC_CODE_BASE + PPC_CODE_SIZE)
                throw std::runtime_error("Function mapping outside generated code interval");
            PPC_LOOKUP_FUNC(memory.base, mapping->guest) = mapping->host;
            ++mapped;
        }
        constexpr uint32_t stack = 0x70000000;
        memory.commit(stack, 0x100000);
        PPCContext context{};
        context.fpscr.loadFromHost();
        context.r1.u64 = stack + 0x100000 - 0x100;
        gh2::kernel::initialize(memory.base, context, (std::filesystem::path(argv[2]).parent_path() / "default.xex").string());
        std::cout << "Mapped " << mapped << " native functions; entering GH2 at 0x823B5010.\n" << std::flush;
        _xstart(context, memory.base);
        gh2::kernel::report();
        gh2::kernel::shutdown();
        std::cout << "Entry returned; this does not establish a successful game boot.\n";
    } catch (const std::exception& error) {
        std::cout << "Trace stopped: " << error.what() << '\n';
        gh2::kernel::report();
        return 2;
    }
}
