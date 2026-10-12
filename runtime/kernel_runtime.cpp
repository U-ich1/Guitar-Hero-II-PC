// Minimal, stateful Xbox loader/kernel services for the native startup path.
// Unsupported services halt; they never return invented success values.
#include "kernel_runtime.h"
#include "game_files.h"
#include "guest_clock.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <atomic>
#include <thread>
#include <chrono>

namespace gh2::kernel {
namespace {
constexpr uint32_t success = 0, invalid_parameter = 0xC000000D, no_memory = 0xC0000017;
struct Allocation { uint32_t size; uint32_t protection; uint32_t page_size; };
std::map<uint32_t, Allocation> allocations;
struct PhysicalAllocation { uint32_t size, address, protection; };
std::map<uint32_t, PhysicalAllocation> physical_allocations;
std::array<bool, 64> tls_used{};
uint32_t tls_slots = 64;
uint32_t system_flags = 0;
uint8_t* memory_base = nullptr;
size_t service_calls = 0;
std::map<std::string, uint64_t> service_counts;
uint32_t notification_position = 0;
std::map<uint32_t, std::unique_ptr<std::recursive_mutex>> critical_sections;
struct WaitObject { HANDLE host; bool semaphore; };
std::map<uint32_t, WaitObject> wait_objects;
uint32_t next_object_handle = 0x10000;
std::recursive_mutex kernel_mutex;
std::atomic<bool> stopping{false};
struct ThreadExit {};
struct GuestThread {
    HANDLE host = nullptr, gate = nullptr;
    uint32_t record, pcr, tls, stack, stack_size, entry, startup, argument, id;
    uint32_t suspended = 0;
};
std::vector<std::unique_ptr<GuestThread>> threads;
std::map<uint32_t, GuestThread*> thread_handles;
std::string worker_error;
DWORD WINAPI run_thread(void* argument) {
    auto& thread = *static_cast<GuestThread*>(argument);
    try {
        while (WaitForSingleObject(thread.gate, 10) == WAIT_TIMEOUT) checkpoint();
        checkpoint();
        PPCContext ctx{};
        ctx.fpscr.loadFromHost();
        ctx.r1.u32 = thread.stack + thread.stack_size - 0x100;
        ctx.r13.u32 = thread.pcr;
        ctx.r3.u32 = thread.startup ? thread.entry : thread.argument;
        ctx.r4.u32 = thread.startup ? thread.argument : 0;
        const uint32_t entry = thread.startup ? thread.startup : thread.entry;
        (PPC_LOOKUP_FUNC(memory_base, entry))(ctx, memory_base);
    } catch (const ThreadExit&) {
    } catch (const std::exception& error) {
        std::lock_guard lock(kernel_mutex);
        if (!stopping && worker_error.empty()) worker_error = error.what();
        stopping = true;
    }
    return 0;
}

uint64_t align_up(uint64_t value, uint32_t alignment) { return (value + alignment - 1) & ~(uint64_t(alignment) - 1); }
uint32_t physical_address(uint32_t address) {
    if (address >= 0x7F000000 && address < 0x80000000) return address - 0x7F000000;
    if (address >= 0xE0000000) return (address - 0xE0000000) + 0x1000;
    if (address >= 0xC0000000) return address - 0xC0000000;
    if (address >= 0xA0000000) return address - 0xA0000000;
    return UINT32_MAX;
}
void protect_physical(uint8_t* base, uint32_t physical, uint32_t size, DWORD protection) {
    const uint32_t addresses[] = {0xA0000000 + physical, 0xC0000000 + physical, 0xE0000000 + physical - 0x1000};
    for (auto address : addresses) {
        DWORD previous;
        if (!VirtualProtect(base + address, size, protection, &previous)) throw std::runtime_error("Physical alias protection failed");
    }
}
uint32_t allocate_physical(uint8_t* base, PPCContext& ctx, bool extended) {
    const uint32_t amount = ctx.r4.u32, protection = ctx.r5.u32;
    if (protection & ~(0xA0000000u | 0x600u | 6u)) throw std::runtime_error("Unsupported physical memory protection flags");
    if (!amount || ((protection & 0xFF) != 2 && (protection & 0xFF) != 4)) return 0;
    const uint32_t page = protection & 0x20000000 ? 0x10000 : protection & 0x80000000 ? 0x1000000 : 0x1000;
    const uint32_t prefix = page == 0x1000 ? 0xE0000000 : page == 0x10000 ? 0xA0000000 : 0xC0000000;
    const uint32_t bias = page == 0x1000 ? 0x1000 : 0;
    const uint32_t requested_alignment = extended ? ctx.r8.u32 : 0;
    if (requested_alignment && (requested_alignment & (requested_alignment - 1))) return 0;
    const uint32_t alignment = std::max(page, requested_alignment);
    const uint64_t size = align_up(amount, page);
    // Low 16 MB is kept for GPU writeback/system memory, not title allocations.
    uint64_t candidate = std::max<uint64_t>(0x1000000, extended ? ctx.r6.u32 : 0);
    const uint64_t end = std::min<uint64_t>(0x1FD00000, extended ? uint64_t(ctx.r7.u32) + 1 : 0x20000000);
    auto aligned = [&](uint64_t value) { return align_up(value - bias, alignment) + bias; };
    candidate = aligned(candidate);
    for (const auto& [start, allocation] : physical_allocations) {
        if (candidate >= uint64_t(start) + allocation.size) continue;
        if (candidate + size <= start) break;
        candidate = aligned(uint64_t(start) + allocation.size);
    }
    if (candidate + size > end) return 0;
    const uint32_t physical = uint32_t(candidate), address = prefix + physical - bias;
    if (!VirtualAlloc(base + address, size, MEM_COMMIT, PAGE_READWRITE)) return 0;
    protect_physical(base, physical, uint32_t(size), PAGE_READWRITE);
    memset(base + address, 0, size);
    if ((protection & 0xFF) == 2) protect_physical(base, physical, uint32_t(size), PAGE_READONLY);
    physical_allocations.emplace(physical, PhysicalAllocation{uint32_t(size), address, protection});
    std::cout << "Physical allocation: guest=0x" << std::hex << address << " physical=0x" << physical << " size=0x" << size << std::dec << '\n';
    return address;
}
void commit(uint8_t* base, uint32_t address, uint32_t size) {
    if (!VirtualAlloc(base + address, size, MEM_COMMIT, PAGE_READWRITE))
        throw std::runtime_error("Kernel state allocation failed");
}
[[noreturn]] void unimplemented(const char* name, const PPCContext& ctx) {
    std::ostringstream message;
    message << "Unimplemented Xbox import " << name << " (caller LR=0x" << std::hex << ctx.lr
            << ", r3=0x" << ctx.r3.u64 << ", r4=0x" << ctx.r4.u64 << ", r5=0x" << ctx.r5.u64 << ")";
    throw std::runtime_error(message.str());
}
uint32_t allocate_virtual(uint8_t* base, PPCContext& ctx) {
    const uint32_t pointer = ctx.r3.u32, size_pointer = ctx.r4.u32;
    const uint32_t flags = ctx.r5.u32, protection = ctx.r6.u32;
    if (!pointer || !size_pointer || !(flags & 0x3000) || ctx.r7.u32) return invalid_parameter;
    // MEM_HEAP marks a heap allocation; it does not change page contents.
    // MEM_NOZERO permits retaining contents; fresh committed Windows pages may
    // still be zero. Existing committed subregions are never cleared here.
    if (flags & ~(0x1000u | 0x2000u | 0x800000u | 0x20000000u | 0x40000000u)) unimplemented("NtAllocateVirtualMemory(flags)", ctx);
    if ((protection & 0xFF) != 2 && (protection & 0xFF) != 4) unimplemented("NtAllocateVirtualMemory(protection)", ctx);
    uint32_t requested = read32(base, pointer), amount = read32(base, size_pointer);
    if (!amount) return invalid_parameter;
    if (amount & 0x80000000) amount = 0u - amount;
    const uint32_t page = requested ? (requested >= 0x40000000 && requested < 0x60000000 ? 65536 : 4096) : (flags & 0x20000000) ? 65536 : 4096;
    const uint32_t region_begin = page == 65536 ? 0x40000000 : 0x10000000;
    const uint32_t region_end = page == 65536 ? 0x60000000 : 0x40000000;
    uint64_t size = align_up(uint64_t(amount) + (requested & (page - 1)), page);
    if (size > 0x30000000) return no_memory;
    uint32_t address = requested & ~(page - 1);
    if (!address) {
        uint64_t candidate = region_begin;
        for (const auto& [start, allocation] : allocations) {
            if (uint64_t(start) + allocation.size <= region_begin) continue;
            if (start >= region_end) break;
            candidate = align_up(candidate, page);
            if (candidate + size <= start) break;
            candidate = std::max(candidate, uint64_t(start) + allocation.size);
        }
        candidate = align_up(candidate, page);
        if (candidate + size > region_end) return no_memory;
        address = uint32_t(candidate);
    }
    if (address < region_begin || uint64_t(address) + size > region_end) return invalid_parameter;
    auto containing = allocations.upper_bound(address);
    if (containing != allocations.begin()) {
        auto previous = std::prev(containing);
        if (uint64_t(address) + size <= uint64_t(previous->first) + previous->second.size && address >= previous->first) {
            if (flags & 0x2000) return 0xC0000018; // conflicting addresses
            if (flags & 0x1000) {
                if (!VirtualAlloc(base + address, size, MEM_COMMIT, (protection & 0xFF) == 2 ? PAGE_READONLY : PAGE_READWRITE)) return no_memory;
            }
            write32(base, pointer, address); write32(base, size_pointer, uint32_t(size));
            return success;
        }
        if (uint64_t(previous->first) + previous->second.size > address) return 0xC0000018;
    }
    if (containing != allocations.end() && uint64_t(address) + size > containing->first) return 0xC0000018;
    if (flags & 0x1000) {
        if (!VirtualAlloc(base + address, size, MEM_COMMIT, (protection & 0xFF) == 2 ? PAGE_READONLY : PAGE_READWRITE)) return no_memory;
    }
    allocations.emplace(address, Allocation{uint32_t(size), protection, page});
    write32(base, pointer, address); write32(base, size_pointer, uint32_t(size));
    return success;
}
uint32_t free_virtual(uint8_t* base, PPCContext& ctx) {
    if (!ctx.r3.u32 || !ctx.r4.u32) return invalid_parameter;
    uint32_t address = read32(base, ctx.r3.u32), size = read32(base, ctx.r4.u32);
    auto found = allocations.find(address);
    if (ctx.r5.u32 == 0x8000) {
        if (found == allocations.end() || size) return invalid_parameter;
        if (!VirtualFree(base + address, found->second.size, MEM_DECOMMIT)) return invalid_parameter;
        allocations.erase(found);
        write32(base, ctx.r3.u32, 0); write32(base, ctx.r4.u32, 0);
        return success;
    }
    if (ctx.r5.u32 != 0x4000) return invalid_parameter;
    auto upper = allocations.upper_bound(address);
    if (upper == allocations.begin()) return invalid_parameter;
    found = std::prev(upper);
    if (!size) size = found->second.size;
    const uint32_t page = found->second.page_size;
    uint32_t aligned_address = address & ~(page - 1);
    uint64_t aligned_size = align_up(uint64_t(size) + (address & (page - 1)), page);
    if (aligned_address < found->first || uint64_t(aligned_address) + aligned_size > uint64_t(found->first) + found->second.size) return invalid_parameter;
    if (!VirtualFree(base + aligned_address, aligned_size, MEM_DECOMMIT)) return invalid_parameter;
    write32(base, ctx.r3.u32, aligned_address); write32(base, ctx.r4.u32, uint32_t(aligned_size));
    return success;
}
}

uint32_t read32(const uint8_t* base, uint32_t address) {
    uint32_t word;
    memcpy(&word, base + address, 4);
    return __builtin_bswap32(word);
}
void write32(uint8_t* base, uint32_t address, uint32_t value) {
    value = __builtin_bswap32(value);
    memcpy(base + address, &value, 4);
}
uint32_t query_header(uint8_t* base, uint32_t header, uint32_t key) {
    if (!header || read32(base, header) != 0x58455832) return 0;
    const uint32_t size = read32(base, header + 8), count = read32(base, header + 20);
    if (size > 0x10000 || uint64_t(count) * 8 + 24 > size) return 0;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t entry = header + 24 + i * 8;
        if (read32(base, entry) != key) continue;
        const uint32_t value = read32(base, entry + 4);
        if ((key & 255) == 0) return value;
        if ((key & 255) == 1) return entry + 4;
        if (value >= size) return 0;
        return header + value;
    }
    return 0;
}
void initialize(uint8_t* base, PPCContext& ctx, const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    std::vector<uint8_t> xex((std::istreambuf_iterator<char>(file)), {});
    if (xex.size() < 24 || memcmp(xex.data(), "XEX2", 4)) throw std::runtime_error("Original XEX is required for loader state");
    auto big = [&](size_t offset) {
        if (offset + 4 > xex.size()) throw std::runtime_error("Truncated XEX metadata");
        uint32_t value; memcpy(&value, xex.data() + offset, 4); return __builtin_bswap32(value);
    };
    const uint32_t header_size = big(8);
    if (header_size > xex.size() || header_size > 0x10000) throw std::runtime_error("Unsupported XEX header size");
    commit(base, state_base, 0x40000);
    memcpy(base + xex_header, xex.data(), header_size);
    write32(base, module_cell, module_record);
    write32(base, module_record + 0x1C, uint32_t(PPC_IMAGE_BASE));
    write32(base, module_record + 0x20, uint32_t(PPC_IMAGE_SIZE));
    write32(base, module_record + 0x38, uint32_t(PPC_IMAGE_SIZE));
    write32(base, module_record + 0x3C, 0x823B5010);
    write32(base, module_record + 0x48, 1); // loader object identity
    write32(base, module_record + 0x58, xex_header);
    // Import slots contain addresses of kernel variables, not variable values.
    if (read32(base, 0x8200070C) != 0x00010193) throw std::runtime_error("Unexpected module-handle import slot for this GH2 build");
    write32(base, 0x8200070C, module_cell);
    // No debug monitor is attached. Its exported variable contains a null pointer.
    write32(base, 0x82000720, state_base + 4);
    write32(base, state_base + 4, 0);
    const std::string command_line = "\"" + std::filesystem::path(path).filename().string() + "\"";
    if (command_line.size() >= 1024) throw std::runtime_error("Loaded command line exceeds kernel buffer");
    memcpy(base + state_base + 0x400, command_line.c_str(), command_line.size() + 1);
    write32(base, 0x82000794, state_base + 0x400);
    system_flags = query_header(base, xex_header, 0x30000);
    const uint32_t tls = query_header(base, xex_header, 0x20104);
    tls_slots = tls ? read32(base, tls) : 64;
    if (!tls_slots || tls_slots > tls_used.size()) throw std::runtime_error("Unsupported TLS slot count");
    const uint32_t template_size = tls ? read32(base, tls + 8) : 0;
    const uint32_t template_address = tls ? read32(base, tls + 4) : 0;
    const uint32_t template_raw_size = tls ? read32(base, tls + 12) : 0;
    if (template_size + tls_slots * 4 > 0x10000 || template_raw_size > template_size) throw std::runtime_error("Invalid TLS template");
    if (template_raw_size) memcpy(base + tls_block, base + template_address, template_raw_size);
    write32(base, pcr_record, tls_block);
    write32(base, pcr_record + 0x30, pcr_record);
    write32(base, pcr_record + 0x70, 0x70100000);
    write32(base, pcr_record + 0x74, 0x70000000);
    write32(base, pcr_record + 0x100, thread_record);
    write32(base, thread_record + 0x5C, 0x70100000);
    write32(base, thread_record + 0x60, 0x70000000);
    write32(base, thread_record + 0x68, tls_block);
    write32(base, thread_record + 0x14C, 1);
    write32(base, thread_record + 0x150, 0x823B5010);
    ctx.r13.u64 = pcr_record;
    stopping = false; worker_error.clear();
    allocations.clear(); critical_sections.clear(); tls_used.fill(false); service_calls = 0; service_counts.clear(); notification_position = 0; memory_base = base;
    physical_allocations.clear();
    wait_objects.clear(); next_object_handle = 0x10000;
    gh2::files::initialize(std::filesystem::path(path).parent_path().string());
    std::cout << "Loader ready: module=0x" << std::hex << module_record << ", XEX header=0x" << xex_header
              << ", PCR=0x" << pcr_record << std::dec << ", TLS slots=" << tls_slots << ".\n";
}
void shutdown() {
    stopping = true;
    for (const auto& thread : threads) SetEvent(thread->gate);
    for (const auto& thread : threads) { WaitForSingleObject(thread->host, INFINITE); CloseHandle(thread->host); CloseHandle(thread->gate); }
    threads.clear(); thread_handles.clear();
    gh2::files::shutdown();
    for (const auto& [id, object] : wait_objects) CloseHandle(object.host);
    wait_objects.clear(); allocations.clear(); critical_sections.clear(); memory_base = nullptr;
}
bool signal_event(uint32_t handle, bool reset) {
    std::lock_guard lock(kernel_mutex);
    auto found = wait_objects.find(handle);
    if (found == wait_objects.end() || found->second.semaphore) return false;
    return reset ? bool(ResetEvent(found->second.host)) : bool(SetEvent(found->second.host));
}
void report() {
    std::lock_guard lock(kernel_mutex);
    if (!worker_error.empty()) std::cout << "Worker stopped: " << worker_error << '\n';
    std::cout << "Kernel service summary (" << service_calls << " calls):\n";
    for (const auto& [name, count] : service_counts) std::cout << "  " << name << ": " << count << '\n';
    gh2::files::report();
}

void checkpoint() {
    if (stopping.load(std::memory_order_relaxed)) throw std::runtime_error("Guest execution stopped; see worker diagnostic");
}

void import(const char* name, PPCContext& ctx, uint8_t* base) {
    checkpoint();
    std::unique_lock lock(kernel_mutex);
    if (base != memory_base) throw std::runtime_error("Kernel used before initialization");
    const std::string service(name);
    const uint32_t current_thread = read32(base, ctx.r13.u32 + 0x100);
    const uint32_t current_tls = read32(base, ctx.r13.u32);
    ++service_calls;
    if (++service_counts[service] <= 3) std::cout << "Kernel " << name << " LR=0x" << std::hex << ctx.lr << std::dec << '\n' << std::flush;
    if (gh2::files::service(service, ctx, base)) return;
    if (service == "MmAllocatePhysicalMemoryEx" || service == "MmAllocatePhysicalMemory") {
        ctx.r3.u64 = allocate_physical(base, ctx, service == "MmAllocatePhysicalMemoryEx"); return;
    }
    if (service == "MmGetPhysicalAddress") { ctx.r3.u64 = physical_address(ctx.r3.u32); return; }
    if (service == "MmFreePhysicalMemory") {
        auto found = physical_allocations.find(physical_address(ctx.r4.u32));
        if (found == physical_allocations.end()) throw std::runtime_error("Invalid physical allocation release");
        // SEC_RESERVE pages stay committed until unmapping. Revoke all aliases,
        // then make this extent reusable by the physical allocator.
        protect_physical(base, found->first, found->second.size, PAGE_NOACCESS);
        physical_allocations.erase(found); return;
    }
    if (service == "MmQueryAllocationSize" || service == "MmQueryAddressProtect") {
        const uint32_t physical = physical_address(ctx.r3.u32);
        auto found = physical_allocations.upper_bound(physical);
        if (found == physical_allocations.begin()) { ctx.r3.u64 = 0; return; }
        --found;
        if (physical >= uint64_t(found->first) + found->second.size) { ctx.r3.u64 = 0; return; }
        ctx.r3.u64 = service == "MmQueryAllocationSize" ? found->second.size : found->second.protection; return;
    }
    if (service == "ExTerminateThread") throw ThreadExit{};
    if (service == "ExCreateThread") {
        const uint32_t output = ctx.r3.u32, id_output = ctx.r5.u32;
        const uint32_t entry = ctx.r6.u32 ? ctx.r6.u32 : ctx.r7.u32;
        if (entry < PPC_CODE_BASE || entry >= PPC_CODE_BASE + PPC_CODE_SIZE || !PPC_LOOKUP_FUNC(base, entry)) unimplemented("ExCreateThread(entry)", ctx);
        if (threads.size() >= 128 || ctx.r4.u32 > 0x100000) { ctx.r3.u64 = no_memory; return; }
        auto thread = std::make_unique<GuestThread>();
        thread->id = uint32_t(threads.size()) + 2;
        thread->record = 0x61000000 + uint32_t(threads.size()) * 0x10000;
        thread->pcr = thread->record + 0x1000; thread->tls = thread->record + 0x2000;
        thread->stack = 0x71000000 + uint32_t(threads.size()) * 0x100000;
        thread->stack_size = uint32_t(align_up((ctx.r4.u32 ? std::max(ctx.r4.u32, 0x4000u) : 0x100000u), 4096));
        thread->startup = ctx.r6.u32; thread->entry = ctx.r7.u32; thread->argument = ctx.r8.u32;
        thread->suspended = ctx.r9.u32 & 1;
        commit(base, thread->record, 0x10000); commit(base, thread->stack, thread->stack_size);
        const uint32_t tls_header = query_header(base, xex_header, 0x20104);
        if (tls_header && read32(base, tls_header + 12)) memcpy(base + thread->tls, base + read32(base, tls_header + 4), read32(base, tls_header + 12));
        write32(base, thread->pcr, thread->tls); write32(base, thread->pcr + 0x30, thread->pcr);
        write32(base, thread->pcr + 0x100, thread->record);
        write32(base, thread->pcr + 0x70, thread->stack + thread->stack_size); write32(base, thread->pcr + 0x74, thread->stack);
        write32(base, thread->record + 0x5C, thread->stack + thread->stack_size); write32(base, thread->record + 0x60, thread->stack);
        write32(base, thread->record + 0x68, thread->tls); write32(base, thread->record + 0x14C, thread->id); write32(base, thread->record + 0x150, thread->entry);
        thread->gate = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!thread->gate) { ctx.r3.u64 = no_memory; return; }
        thread->host = CreateThread(nullptr, 16 * 1024 * 1024, run_thread, thread.get(), 0, nullptr);
        if (!thread->host) { CloseHandle(thread->gate); ctx.r3.u64 = no_memory; return; }
        const uint32_t handle = next_object_handle++;
        thread_handles[handle] = thread.get();
        if (output) write32(base, output, ctx.r9.u32 & 0x80 ? thread->record : handle);
        if (id_output) write32(base, id_output, thread->id);
        if (!thread->suspended) SetEvent(thread->gate);
        std::cout << "Created guest worker " << thread->id << " entry=0x" << std::hex << entry << std::dec << '\n';
        threads.push_back(std::move(thread)); ctx.r3.u64 = 0; return;
    }
    if (service == "NtResumeThread" || service == "KeResumeThread") {
        GuestThread* thread = nullptr;
        if (service == "NtResumeThread") { auto it = thread_handles.find(ctx.r3.u32); if (it != thread_handles.end()) thread = it->second; }
        else for (auto& item : threads) if (item->record == ctx.r3.u32) thread = item.get();
        if (!thread) { ctx.r3.u64 = 0xC0000008; return; }
        if (service == "NtResumeThread" && ctx.r4.u32) write32(base, ctx.r4.u32, thread->suspended);
        thread->suspended = 0; SetEvent(thread->gate); ctx.r3.u64 = 0; return;
    }
    if (service == "NtCreateSemaphore" || service == "NtCreateEvent") {
        if (!ctx.r3.u32) { ctx.r3.u64 = invalid_parameter; return; }
        if (ctx.r4.u32) unimplemented("Named kernel wait object", ctx);
        HANDLE host = nullptr;
        const bool semaphore = service == "NtCreateSemaphore";
        if (semaphore) host = CreateSemaphoreW(nullptr, ctx.r5.s32, ctx.r6.s32, nullptr);
        else if (ctx.r5.u32 <= 1) host = CreateEventW(nullptr, ctx.r5.u32 == 0, ctx.r6.u32 != 0, nullptr);
        if (!host) { write32(base, ctx.r3.u32, 0); ctx.r3.u64 = invalid_parameter; return; }
        const uint32_t handle = next_object_handle++;
        wait_objects.emplace(handle, WaitObject{host, semaphore});
        write32(base, ctx.r3.u32, handle); ctx.r3.u64 = 0; return;
    }
    if (service == "NtReleaseSemaphore") {
        const auto found = wait_objects.find(ctx.r3.u32);
        if (found == wait_objects.end() || !found->second.semaphore) { ctx.r3.u64 = 0xC0000008; return; }
        LONG previous = 0;
        if (!ReleaseSemaphore(found->second.host, ctx.r4.s32, &previous)) { ctx.r3.u64 = 0xC0000047; return; }
        if (ctx.r5.u32) write32(base, ctx.r5.u32, previous);
        ctx.r3.u64 = 0; return;
    }
    if (service == "NtWaitForSingleObjectEx") {
        const auto found = wait_objects.find(ctx.r3.u32);
        const auto thread = thread_handles.find(ctx.r3.u32);
        HANDLE source = found != wait_objects.end() ? found->second.host : thread != thread_handles.end() ? thread->second->host : nullptr;
        if (!source) { ctx.r3.u64 = 0xC0000008; return; }
        if (ctx.r5.u32) unimplemented("Alertable kernel wait", ctx);
        DWORD timeout = INFINITE;
        if (ctx.r6.u32) {
            const uint64_t raw = (uint64_t(read32(base, ctx.r6.u32)) << 32) | read32(base, ctx.r6.u32 + 4);
            uint64_t duration;
            if (int64_t(raw) <= 0) duration = 0u - raw;
            else {
                FILETIME now{}; GetSystemTimePreciseAsFileTime(&now);
                const uint64_t time = (uint64_t(now.dwHighDateTime) << 32) | now.dwLowDateTime;
                duration = raw > time ? raw - time : 0;
            }
            timeout = DWORD(std::min<uint64_t>((duration + 9999) / 10000, INFINITE - 1));
        }
        HANDLE waiting = nullptr;
        if (!DuplicateHandle(GetCurrentProcess(), source, GetCurrentProcess(), &waiting, 0, FALSE, DUPLICATE_SAME_ACCESS)) throw std::runtime_error("Wait handle duplication failed");
        lock.unlock();
        DWORD result = WAIT_TIMEOUT;
        const auto begin = GetTickCount64();
        do {
            if (stopping) { CloseHandle(waiting); checkpoint(); }
            const auto elapsed = GetTickCount64() - begin;
            const DWORD remaining = timeout == INFINITE ? 10 : DWORD(elapsed >= timeout ? 0 : std::min<uint64_t>(10, timeout - elapsed));
            result = WaitForSingleObject(waiting, remaining);
            if (result != WAIT_TIMEOUT || (timeout != INFINITE && GetTickCount64() - begin >= timeout)) break;
        } while (true);
        CloseHandle(waiting);
        ctx.r3.u64 = result == WAIT_OBJECT_0 ? 0 : result == WAIT_TIMEOUT ? 0x102 : 0xC0000001;
        return;
    }
    if (service == "NtClose") {
        if (thread_handles.erase(ctx.r3.u32)) { ctx.r3.u64 = 0; return; }
        auto found = wait_objects.find(ctx.r3.u32);
        if (found == wait_objects.end()) { ctx.r3.u64 = 0xC0000008; return; }
        CloseHandle(found->second.host); wait_objects.erase(found); ctx.r3.u64 = 0; return;
    }
    if (service == "NtSetEvent" || service == "NtClearEvent") {
        ctx.r3.u64 = signal_event(ctx.r3.u32, service == "NtClearEvent") ? 0 : 0xC0000008; return;
    }
    if (service == "RtlImageXexHeaderField") { ctx.r3.u64 = query_header(base, ctx.r3.u32, ctx.r4.u32); return; }
    if (service == "XexCheckExecutablePrivilege") { ctx.r3.u64 = ctx.r3.u32 < 32 && (system_flags & (1u << ctx.r3.u32)) ? 1 : 0; return; }
    if (service == "NtAllocateVirtualMemory") { ctx.r3.u64 = allocate_virtual(base, ctx); return; }
    if (service == "NtFreeVirtualMemory") { ctx.r3.u64 = free_virtual(base, ctx); return; }
    if (service == "KeGetCurrentProcessType") { ctx.r3.u64 = 1; return; } // User title process.
    if (service == "KeQueryPerformanceFrequency") { ctx.r3.u64 = gh2_timebase_frequency; return; }
    if (service == "KeQuerySystemTime") {
        FILETIME value; GetSystemTimePreciseAsFileTime(&value);
        if (ctx.r3.u32) {
            write32(base, ctx.r3.u32, value.dwHighDateTime);
            write32(base, ctx.r3.u32 + 4, value.dwLowDateTime);
        }
        return;
    }
    if (service == "RtlTimeToTimeFields") {
        FILETIME filetime{read32(base, ctx.r3.u32 + 4), read32(base, ctx.r3.u32)};
        SYSTEMTIME time{};
        if (!FileTimeToSystemTime(&filetime, &time)) throw std::runtime_error("Invalid guest FILETIME");
        const uint16_t fields[] = {time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds, time.wDayOfWeek};
        for (size_t i = 0; i < 8; ++i) { const uint16_t value = __builtin_bswap16(fields[i]); memcpy(base + ctx.r4.u32 + i * 2, &value, 2); }
        return;
    }
    if (service == "RtlTimeFieldsToTime") {
        uint16_t fields[8];
        for (size_t i = 0; i < 8; ++i) { memcpy(&fields[i], base + ctx.r3.u32 + i * 2, 2); fields[i] = __builtin_bswap16(fields[i]); }
        SYSTEMTIME time{fields[0], fields[1], fields[7], fields[2], fields[3], fields[4], fields[5], fields[6]};
        FILETIME result{};
        if (!SystemTimeToFileTime(&time, &result)) { ctx.r3.u64 = 0; return; }
        write32(base, ctx.r4.u32, result.dwHighDateTime); write32(base, ctx.r4.u32 + 4, result.dwLowDateTime);
        ctx.r3.u64 = 1; return;
    }
    // Startup environment: virtual VGA AV connection and English language.
    // These describe configuration; graphics calls remain unsupported.
    if (service == "XGetAVPack") { ctx.r3.u64 = 6; return; }
    if (service == "XGetVideoMode" || service == "VdQueryVideoMode") {
        // Requested compatibility mode; rendering is still a separate backend.
        const uint32_t fields[12] = {1280, 720, 0, 1, 1, 0x42700000, 1, 0x4A, 1, 0, 0, 0};
        for (uint32_t i = 0; i < 12; ++i) write32(base, ctx.r3.u32 + i * 4, fields[i]);
        return;
    }
    if (service == "XGetLanguage") { ctx.r3.u64 = 1; return; }
    if (service == "XNotifyPositionUI") { notification_position = ctx.r3.u32; return; }
    if (service == "RtlInitAnsiString") {
        const uint32_t output = ctx.r3.u32, source = ctx.r4.u32;
        size_t length = source ? strnlen(reinterpret_cast<const char*>(base + source), 65534) : 0;
        uint16_t value = __builtin_bswap16(uint16_t(length)); memcpy(base + output, &value, 2);
        value = __builtin_bswap16(uint16_t(source ? length + 1 : 0)); memcpy(base + output + 2, &value, 2);
        write32(base, output + 4, source); return;
    }
    if (service == "RtlNtStatusToDosError") {
        using Converter = ULONG (WINAPI*)(LONG);
        static auto convert = reinterpret_cast<Converter>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlNtStatusToDosError"));
        if (!convert) throw std::runtime_error("Host NTSTATUS conversion unavailable");
        ctx.r3.u64 = convert(LONG(ctx.r3.u32)); return;
    }
    if (service == "ExGetXConfigSetting") {
        const uint16_t category = ctx.r3.u16, setting = ctx.r4.u16;
        uint32_t value = 0;
        // Defined compatibility environment: UTC (no DST), English, NTSC-U.
        // UTC timezone bias, four-byte names, transition dates and extra biases
        // all have zero encodings. This does not read/alter Windows settings.
        if (category == 3 && setting >= 1 && setting <= 7) value = 0;
        else if (category == 3 && setting == 9) value = 1;
        else if (category == 2 && setting == 2) value = 0x1000;
        else if (category == 3 && setting == 10) value = 0x40000;
        else if (category == 3 && setting == 12) value = 0; // Default retail user flags.
        else unimplemented("ExGetXConfigSetting(category/setting)", ctx);
        if (ctx.r7.u32) { uint16_t required = __builtin_bswap16(uint16_t(4)); memcpy(base + ctx.r7.u32, &required, 2); }
        if (ctx.r5.u32 && ctx.r6.u16 < 4) { ctx.r3.u64 = 0xC0000023; return; }
        if (!ctx.r5.u32 && ctx.r6.u16) { ctx.r3.u64 = invalid_parameter; return; }
        if (ctx.r5.u32) write32(base, ctx.r5.u32, value);
        ctx.r3.u64 = 0; return;
    }
    if (service == "XamGetExecutionId") {
        const auto output = ctx.r3.u32;
        const auto execution = query_header(base, xex_header, 0x40006);
        if (!output || !execution) { ctx.r3.u64 = invalid_parameter; return; }
        write32(base, output, execution); ctx.r3.u64 = 0; return;
    }
    if (service == "RtlInitializeCriticalSection") {
        const auto address = ctx.r3.u32;
        memset(base + address, 0, 28); base[address] = 1;
        write32(base, address + 16, UINT32_MAX);
        critical_sections[address] = std::make_unique<std::recursive_mutex>();
        return;
    }
    if (service == "RtlEnterCriticalSection" || service == "RtlTryEnterCriticalSection" || service == "RtlLeaveCriticalSection") {
        const auto address = ctx.r3.u32;
        auto& mutex = critical_sections[address];
        if (!mutex) mutex = std::make_unique<std::recursive_mutex>();
        const uint32_t recursion = read32(base, address + 20);
        const uint32_t owner = read32(base, address + 24);
        if (service == "RtlLeaveCriticalSection") {
            if (!recursion || owner != current_thread) throw std::runtime_error("Critical section released by non-owner");
            write32(base, address + 20, recursion - 1);
            write32(base, address + 16, read32(base, address + 16) - 1);
            if (recursion == 1) write32(base, address + 24, 0);
            mutex->unlock(); return;
        }
        if (service == "RtlTryEnterCriticalSection" && !mutex->try_lock()) { ctx.r3.u64 = 0; return; }
        if (service == "RtlEnterCriticalSection") {
            auto* waiting = mutex.get();
            lock.unlock();
            while (!waiting->try_lock()) { checkpoint(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
            lock.lock();
        }
        write32(base, address + 16, read32(base, address + 16) + 1);
        write32(base, address + 20, read32(base, address + 20) + 1);
        write32(base, address + 24, current_thread);
        if (service == "RtlTryEnterCriticalSection") ctx.r3.u64 = 1;
        return;
    }
    if (service == "KeTlsAlloc") {
        for (uint32_t i = 0; i < tls_slots; ++i) if (!tls_used[i]) { tls_used[i] = true; write32(base, current_tls + i * 4, 0); ctx.r3.u64 = i; return; }
        ctx.r3.u64 = UINT32_MAX; return;
    }
    if (service == "KeTlsFree") {
        const uint32_t i = ctx.r3.u32;
        if (i >= tls_slots || !tls_used[i]) { ctx.r3.u64 = 0; return; }
        tls_used[i] = false; write32(base, tls_block + i * 4, 0);
        for (const auto& thread : threads) write32(base, thread->tls + i * 4, 0);
        ctx.r3.u64 = 1; return;
    }
    if (service == "KeTlsGetValue") { ctx.r3.u64 = ctx.r3.u32 < tls_slots ? read32(base, current_tls + ctx.r3.u32 * 4) : 0; return; }
    if (service == "KeTlsSetValue") {
        const uint32_t i = ctx.r3.u32;
        if (i >= tls_slots) { ctx.r3.u64 = 0; return; }
        write32(base, current_tls + i * 4, ctx.r4.u32); ctx.r3.u64 = 1; return;
    }
    unimplemented(name, ctx);
}
}

