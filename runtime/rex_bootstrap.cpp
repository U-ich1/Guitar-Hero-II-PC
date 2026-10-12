// Diagnostic GH2 runner using the real SDK context, kernel and Xenos backend.
#include "ppc_recomp_shared.h"
#include <rex/runtime.h>
#include <rex/system/gpu_plugin.h>
#include <rex/system/xthread.h>
#include <rex/logging.h>
#include <rex/kernel/init.h>
#include <rex/audio/sdl/sdl_audio_system.h>
#include <rex/input/input_system.h>
#include <rex/thread.h>
#include <windows.h>
#include <iostream>
#include <atomic>
#include <cstdlib>

namespace {
thread_local PPCContext* active_context;
thread_local const char* recent[32]{};
thread_local size_t count;
uint8_t* memory_base;
std::atomic<const char*> main_function{nullptr};
std::atomic<uint64_t> main_lr{0};
LONG CALLBACK fault(EXCEPTION_POINTERS* info) {
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION &&
        info->ExceptionRecord->ExceptionCode != EXCEPTION_ILLEGAL_INSTRUCTION) return EXCEPTION_CONTINUE_SEARCH;
    // SDK MMIO handlers are registered first and get the opportunity to handle GPU accesses.
    std::cerr << "GH2 SDK diagnostic fault 0x" << std::hex << info->ExceptionRecord->ExceptionCode;
    if (info->ExceptionRecord->NumberParameters >= 2) {
        const auto address = info->ExceptionRecord->ExceptionInformation[1];
        if (address >= uintptr_t(memory_base) && address - uintptr_t(memory_base) < 0x100000000ULL)
            std::cerr << " guest address 0x" << address - uintptr_t(memory_base);
    }
    if (active_context) std::cerr << " LR=0x" << active_context->lr;
    std::cerr << "\nRecent guest functions:\n";
    for (size_t i = count > 32 ? count - 32 : 0; i < count; ++i) std::cerr << recent[i % 32] << '\n';
    rex::FlushLogging();
    std::cerr << std::flush;
    std::_Exit(5);
}
}
void gh2_trace_entry(PPCContext& ctx, const char* name) {
#ifdef GH2_REX_WINDOWED
    static const bool detailed=std::getenv("GH2_TRACE_RECOMP")!=nullptr;
    if(!detailed){rex::system::XThread::CheckTitleTermination();return;}
#endif
    active_context = &ctx;
    recent[count++ % 32] = name;
    auto* thread = rex::system::XThread::GetCurrentThread();
    if (thread && thread->main_thread()) {
        main_function.store(name, std::memory_order_relaxed);
        main_lr.store(ctx.lr, std::memory_order_relaxed);
    }
    rex::system::XThread::CheckTitleTermination();
}
void gh2_rex_register_imports(rex::Runtime&);
void verify_physical_aliases(rex::memory::Memory& memory) {
    GH2ReXMemoryBase base{memory.virtual_membase()};
    for (const auto [heap_address, page_size] : {std::pair{0xE0000000U, 0x1000U},
         std::pair{0xA0000000U, 0x10000U}, std::pair{0xC0000000U, 0x1000000U}}) {
        auto* heap = memory.LookupHeap(heap_address);
        uint32_t guest = 0;
        if (!heap->Alloc(page_size, page_size,
            rex::memory::kMemoryAllocationReserve | rex::memory::kMemoryAllocationCommit,
            rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectWrite, false, &guest))
            throw std::runtime_error("SDK physical alias test allocation failed");
        const uint32_t physical = memory.GetPhysicalAddress(guest);
        if (base + guest != memory.TranslateVirtual(guest)) throw std::runtime_error("SDK CPU pointer mismatch");
        PPC_STORE_U32(guest, 0xA1562034U);
        if (rex::memory::load_and_swap<uint32_t>(memory.TranslatePhysical(physical)) != 0xA1562034U)
            throw std::runtime_error("CPU/GPU scalar physical alias mismatch");
        const auto vector = simde_mm_set1_epi32(0x56789ABC);
        simde_mm_store_si128(reinterpret_cast<simde__m128i*>(base + (guest + page_size - 16)), vector);
        if (std::memcmp(memory.TranslatePhysical(physical + page_size - 16), base + (guest + page_size - 16), 16))
            throw std::runtime_error("CPU/GPU vector physical alias mismatch");
        rex::memory::store_and_swap<uint32_t>(memory.TranslatePhysical(physical), 0x98765432U);
        if (PPC_LOAD_U32(guest) != 0x98765432U) throw std::runtime_error("GPU/CPU writeback mismatch");
        heap->Release(guest);
    }
    std::cout << "SDK physical alias checks passed for 4 KB, 64 KB and 16 MB pages.\n" << std::flush;
}
void gh2_rex_attach_memory(rex::Runtime& runtime) {
    memory_base = runtime.virtual_membase();
    verify_physical_aliases(*runtime.memory());
    AddVectoredExceptionHandler(0, fault);
}
#ifndef GH2_REX_WINDOWED
void gh2_request_menu_quit() { REXSYS_INFO("GH2 menu Quit requested in diagnostic runner"); }
int main(int argc, char** argv) {
    if (argc != 3) { std::cerr << "Usage: gh2_rex_bootstrap <game-data-directory> <scratch-directory>\n"; return 1; }
    try {
        const std::filesystem::path scratch(argv[2]);
        std::filesystem::create_directories(scratch);
        rex::InitLogging((scratch / "gh2.log").string().c_str(), spdlog::level::debug);
        struct LoggingGuard { ~LoggingGuard() { rex::ShutdownLogging(); } } logging_guard;
        rex::RuntimeConfig config;
        config.kernel_init = rex::kernel::InitializeKernel;
        config.audio_factory = REX_AUDIO_BACKEND(rex::audio::sdl::SDLAudioSystem);
        config.input_factory = REX_INPUT_BACKEND(rex::input::CreateDefaultInputSystem);
        config.graphics = rex::system::LoadGpuPlugin("xenos", "d3d12");
        if (!config.graphics) return 2;
        rex::Runtime runtime(argv[1], scratch / "user", {}, scratch / "cache");
        rex::PPCImageInfo image{PPC_CODE_BASE, PPC_CODE_SIZE, PPC_IMAGE_BASE, PPC_IMAGE_SIZE, PPCFuncMappings};
        if (runtime.Setup(image, std::move(config)) != 0) return 3;
        gh2_rex_attach_memory(runtime);
        if (runtime.LoadXexImage("game:\\default.xex") != 0) return 4;
        gh2_rex_register_imports(runtime);
        auto thread = runtime.PrepareModuleLaunch();
        if (!thread) return 4;
        std::cout << "Starting GH2 with the Xenos backend; headless diagnostic, no presentation.\n" << std::flush;
        thread->Resume();
        const auto result = rex::thread::Wait(thread->thread(), false, std::chrono::seconds(30));
        if (result == rex::thread::WaitResult::kTimeout) {
            std::cout << "Main guest checkpoint: " << (main_function.load() ? main_function.load() : "unknown")
                      << " LR=0x" << std::hex << main_lr.load() << std::dec << '\n';
            std::cout << "30-second diagnostic limit reached; requesting title termination.\n" << std::flush;
            // Drain host GPU/audio callbacks while the title's code is registered.
            if (runtime.audio_system()) runtime.audio_system()->Shutdown();
            if (runtime.graphics_system()) runtime.graphics_system()->Shutdown();
            runtime.kernel_state()->TerminateTitle();
        }
        runtime.Shutdown();
        std::cout << "Diagnostic ended; inspect the log for the verified boot checkpoint.\n";
        return result == rex::thread::WaitResult::kTimeout ? 6 : 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 7; }
}
#endif
