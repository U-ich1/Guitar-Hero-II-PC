#include "kernel_runtime.h"
#include "guest_clock.h"
#include "guest_memory.h"
#include <windows.h>
#include <fstream>
#include <iterator>
#include <vector>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace gh2::kernel;

void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void call(const char* service, PPCContext& ctx, uint8_t* base) { gh2::kernel::import(service, ctx, base); }
void worker_fixture(PPCContext& ctx, uint8_t* base) {
    const uint32_t value = ctx.r3.u32;
    ctx.r3.u32 = 0; ctx.r4.u32 = value; call("KeTlsSetValue", ctx, base);
    for (unsigned i = 0; i < 1000; ++i) {
        ctx.r3.u32 = 0x70003000; call("RtlEnterCriticalSection", ctx, base);
        write32(base, 0x70003100, read32(base, 0x70003100) + 1);
        ctx.r3.u32 = 0; call("KeTlsGetValue", ctx, base);
        require(ctx.r3.u32 == value, "Worker TLS leaked across threads");
        ctx.r3.u32 = 0x70003000; call("RtlLeaveCriticalSection", ctx, base);
    }
}
struct Memory : GuestAddressSpace {
    ~Memory() { shutdown(); }
};
int main() {
    try {
        Memory memory;
        memory.commit(uint32_t(PPC_IMAGE_BASE), uint32_t(PPC_IMAGE_SIZE));
        memory.commit(0x70000000, 0x100000);
        std::ifstream image("private/default.exe", std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(image)), {});
        require(!bytes.empty(), "Decoded image missing");
        memcpy(memory.base + PPC_IMAGE_BASE, bytes.data(), bytes.size());
        PPCContext ctx{}; ctx.r1.u32 = 0x70004000;
        initialize(memory.base, ctx, "private/default.xex");
        auto* base = memory.base;
        require(read32(base, read32(base, 0x8200070C)) == module_record, "Module import must point to module cell");
        require(read32(base, module_record + 0x58) == xex_header, "LDR header address mismatch");
        require(query_header(base, xex_header, 0x10100) == 0x823B5010, "Immediate optional header mismatch");
        require(read32(base, query_header(base, xex_header, 0x10201)) == PPC_IMAGE_BASE, "Inline-pointer optional header mismatch");
        require(read32(base, query_header(base, xex_header, 0x40006) + 12) == 0x415607E7, "Execution title ID mismatch");
        require(query_header(base, xex_header, 0xDEADFF) == 0, "Missing optional header must return null");
        require(ctx.r13.u32 == pcr_record && read32(base, pcr_record + 0x100) == thread_record, "PCR current-thread layout mismatch");
        require(std::string(reinterpret_cast<char*>(base + read32(base, 0x82000794))) == "\"default.xex\"", "Command line mismatch");

        for (uint32_t i = 0; i < 64; ++i) {
            call("KeTlsAlloc", ctx, base); require(ctx.r3.u32 == i, "TLS slot allocation mismatch");
            ctx.r4.u32 = i ^ 0xA55AA55A; call("KeTlsSetValue", ctx, base); require(ctx.r3.u32 == 1, "TLS set failed");
            ctx.r3.u32 = i; call("KeTlsGetValue", ctx, base); require(ctx.r3.u32 == (i ^ 0xA55AA55A), "TLS round trip mismatch");
        }
        call("KeTlsAlloc", ctx, base); require(ctx.r3.u32 == UINT32_MAX, "TLS exhaustion not reported");
        ctx.r3.u32 = 7; call("KeTlsFree", ctx, base); call("KeTlsAlloc", ctx, base); require(ctx.r3.u32 == 7, "TLS free/reuse mismatch");
        ctx.r3.u32 = 7; call("KeTlsGetValue", ctx, base); require(ctx.r3.u32 == 0, "Reused TLS slot not cleared");

        constexpr uint32_t pointer = 0x70001000, size_pointer = pointer + 4;
        write32(base, pointer, 0); write32(base, size_pointer, 4097);
        ctx.r3.u32 = pointer; ctx.r4.u32 = size_pointer; ctx.r5.u32 = 0x60002000; ctx.r6.u32 = 4; ctx.r7.u32 = 0;
        call("NtAllocateVirtualMemory", ctx, base); require(ctx.r3.u32 == 0, "Heap reservation failed");
        const uint32_t allocated = read32(base, pointer);
        require(!(allocated & 65535) && read32(base, size_pointer) == 65536, "Heap page alignment mismatch");
        MEMORY_BASIC_INFORMATION info{}; VirtualQuery(base + allocated, &info, sizeof(info)); require(info.State == MEM_RESERVE, "Reserve must not commit pages");
        write32(base, pointer, allocated + 4096); write32(base, size_pointer, 17);
        ctx.r3.u32 = pointer; ctx.r4.u32 = size_pointer; ctx.r5.u32 = 0x60001000; ctx.r6.u32 = 4;
        call("NtAllocateVirtualMemory", ctx, base); require(ctx.r3.u32 == 0 && read32(base, size_pointer) == 65536 && read32(base, pointer) == allocated, "Large-page subregion commit failed");
        require(read32(base, allocated + 4096) == 0, "Committed memory not zero initialized");
        write32(base, allocated + 4096, 0x12345678);
        write32(base, pointer, allocated); write32(base, size_pointer, 65536);
        ctx.r3.u32 = pointer; ctx.r4.u32 = size_pointer; ctx.r5.u32 = 0x2000; ctx.r6.u32 = 4;
        call("NtAllocateVirtualMemory", ctx, base); require(ctx.r3.u32 == 0xC0000018, "Overlapping reservation not rejected");
        write32(base, pointer, allocated + 4096); write32(base, size_pointer, 4096);
        ctx.r3.u32 = pointer; ctx.r4.u32 = size_pointer; ctx.r5.u32 = 0x4000;
        call("NtFreeVirtualMemory", ctx, base); require(ctx.r3.u32 == 0, "Decommit failed");
        VirtualQuery(base + allocated + 4096, &info, sizeof(info)); require(info.State == MEM_RESERVE, "Decommit must remove access");
        write32(base, pointer, allocated); write32(base, size_pointer, 0);
        ctx.r3.u32 = pointer; ctx.r4.u32 = size_pointer; ctx.r5.u32 = 0x8000;
        call("NtFreeVirtualMemory", ctx, base); require(ctx.r3.u32 == 0 && read32(base, pointer) == 0, "Release failed");

        constexpr uint32_t cs = 0x70002000;
        ctx.r3.u32 = cs; call("RtlInitializeCriticalSection", ctx, base);
        call("RtlEnterCriticalSection", ctx, base); call("RtlEnterCriticalSection", ctx, base);
        require(read32(base, cs + 20) == 2 && read32(base, cs + 24) == thread_record, "Recursive critical-section ownership mismatch");
        call("RtlTryEnterCriticalSection", ctx, base); require(ctx.r3.u32 == 1 && read32(base, cs + 20) == 3, "Recursive try-enter failed");
        ctx.r3.u32 = cs; call("RtlLeaveCriticalSection", ctx, base); call("RtlLeaveCriticalSection", ctx, base); call("RtlLeaveCriticalSection", ctx, base);
        require(read32(base, cs + 16) == UINT32_MAX && read32(base, cs + 24) == 0, "Final critical-section release mismatch");
        bool rejected = false; try { call("RtlLeaveCriticalSection", ctx, base); } catch (const std::runtime_error&) { rejected = true; }
        require(rejected, "Non-owned critical-section release must fail");

        const uint32_t name = 0x70003000, string = name + 256, attributes = name + 288, output = name + 304, ios = name + 320;
        auto open = [&](const char* path, uint32_t access) {
            strcpy(reinterpret_cast<char*>(base + name), path);
            ctx.r3.u32 = string; ctx.r4.u32 = name; call("RtlInitAnsiString", ctx, base);
            write32(base, attributes, 0xFFFFFFFD); write32(base, attributes + 4, string); write32(base, attributes + 8, 0x40);
            write32(base, ctx.r1.u32 + 0x54, 0x40); // Asynchronous, non-directory.
            ctx.r3.u32 = output; ctx.r4.u32 = access; ctx.r5.u32 = attributes; ctx.r6.u32 = ios;
            ctx.r7.u32 = 0; ctx.r8.u32 = 0; ctx.r9.u32 = 1; ctx.r10.u32 = 1; call("NtCreateFile", ctx, base);
            return ctx.r3.u32;
        };
        require(open("d:\\gen\\main.hdr", 0x80100080) == 0, "Real disc file could not be opened");
        const auto file = read32(base, output);
        ctx.r3.u32 = file; ctx.r4.u32 = ios; ctx.r5.u32 = name + 400; ctx.r6.u32 = 56; ctx.r7.u32 = 34;
        call("NtQueryInformationFile", ctx, base);
        require(ctx.r3.u32 == 0 && read32(base, name + 444) == std::filesystem::file_size("private/gen/main.hdr"), "File size metadata mismatch");
        ctx.r3.u32 = file; ctx.r4.u32 = 0; ctx.r5.u32 = 0; ctx.r6.u32 = 0; ctx.r7.u32 = ios; ctx.r8.u32 = name + 512; ctx.r9.u32 = 32; ctx.r10.u32 = 0;
        call("NtReadFile", ctx, base); require(ctx.r3.u32 == 0 && read32(base, ios + 4) == 32, "Immediate-completion read failed");
        std::ifstream expected_file("private/gen/main.hdr", std::ios::binary); uint8_t expected[32]; expected_file.read(reinterpret_cast<char*>(expected), 32);
        require(!memcmp(expected, base + name + 512, 32), "Disc read returned wrong bytes");
        constexpr uint32_t object_out = 0x70002500, timeout_pointer = object_out + 8;
        write32(base, timeout_pointer, 0); write32(base, timeout_pointer + 4, 0);
        ctx.r3.u32 = object_out; ctx.r4.u32 = 0; ctx.r5.u32 = 1; ctx.r6.u32 = 0;
        call("NtCreateEvent", ctx, base); require(ctx.r3.u32 == 0, "Event creation failed");
        const auto event = read32(base, object_out);
        ctx.r3.u32 = event; ctx.r4.u32 = 0; ctx.r5.u32 = 0; ctx.r6.u32 = timeout_pointer;
        call("NtWaitForSingleObjectEx", ctx, base); require(ctx.r3.u32 == 0x102, "Unsignalled event must time out");
        ctx.r3.u32 = file; ctx.r4.u32 = event; ctx.r5.u32 = 0; ctx.r6.u32 = 0; ctx.r7.u32 = ios; ctx.r8.u32 = name + 512; ctx.r9.u32 = 16; ctx.r10.u32 = 0;
        call("NtReadFile", ctx, base); require(ctx.r3.u32 == 0 && read32(base, ios + 4) == 16, "Event-backed file completion failed");
        ctx.r3.u32 = event; ctx.r4.u32 = 0; ctx.r5.u32 = 0; ctx.r6.u32 = timeout_pointer;
        call("NtWaitForSingleObjectEx", ctx, base); require(ctx.r3.u32 == 0, "File completion did not signal the event");
        ctx.r3.u32 = event; call("NtWaitForSingleObjectEx", ctx, base); require(ctx.r3.u32 == 0x102, "Auto-reset event did not consume its signal");
        ctx.r3.u32 = event; call("NtClose", ctx, base);
        const uint64_t end = std::filesystem::file_size("private/gen/main.hdr") - 2;
        write32(base, name + 600, end >> 32); write32(base, name + 604, uint32_t(end));
        ctx.r3.u32 = file; ctx.r4.u32 = ios; ctx.r5.u32 = name + 600; ctx.r6.u32 = 8; ctx.r7.u32 = 14;
        call("NtSetInformationFile", ctx, base); require(ctx.r3.u32 == 0, "File seek failed");
        ctx.r3.u32 = file; ctx.r4.u32 = 0; ctx.r5.u32 = 0; ctx.r6.u32 = 0; ctx.r7.u32 = ios; ctx.r8.u32 = name + 512; ctx.r9.u32 = 8; ctx.r10.u32 = 0;
        call("NtReadFile", ctx, base); require(ctx.r3.u32 == 0 && read32(base, ios + 4) == 2, "Short read at EOF mismatch");
        ctx.r3.u32 = file; call("NtReadFile", ctx, base); require(ctx.r3.u32 == 0xC0000011 && read32(base, ios + 4) == 0, "EOF status mismatch");
        ctx.r3.u32 = file; call("NtClose", ctx, base);
        require(open("d:\\..\\README.md", 0x80100080) == 0xC000003A, "Path traversal escaped the disc root");
        require(open("C:\\Windows\\win.ini", 0x80100080) == 0xC000003A, "Unmapped host path accepted");
        require(open("game:\\gen\\main.hdr", 0x40000000) == 0xC0000022, "Disc writes must be rejected");

        ctx.r3.u32 = object_out; ctx.r4.u32 = 0; ctx.r5.u32 = 0; ctx.r6.u32 = 2;
        call("NtCreateSemaphore", ctx, base); require(ctx.r3.u32 == 0, "Semaphore creation failed");
        const auto semaphore = read32(base, object_out);
        ctx.r3.u32 = semaphore; ctx.r4.u32 = 0; ctx.r5.u32 = 0; ctx.r6.u32 = timeout_pointer;
        call("NtWaitForSingleObjectEx", ctx, base); require(ctx.r3.u32 == 0x102, "Empty semaphore must time out");
        ctx.r3.u32 = semaphore; ctx.r4.u32 = 1; ctx.r5.u32 = object_out + 4;
        call("NtReleaseSemaphore", ctx, base); require(ctx.r3.u32 == 0 && read32(base, object_out + 4) == 0, "Semaphore release count mismatch");
        ctx.r3.u32 = semaphore; ctx.r4.u32 = 0; ctx.r5.u32 = 0; ctx.r6.u32 = timeout_pointer;
        call("NtWaitForSingleObjectEx", ctx, base); require(ctx.r3.u32 == 0, "Semaphore did not release a waiter");
        ctx.r3.u32 = semaphore; ctx.r4.u32 = 3; ctx.r5.u32 = 0;
        call("NtReleaseSemaphore", ctx, base); require(ctx.r3.u32 == 0xC0000047, "Semaphore limit overflow not rejected");
        ctx.r3.u32 = semaphore; call("NtClose", ctx, base);

        constexpr uint32_t date_fields = 0x70002600, file_time = date_fields + 32, recovered = date_fields + 48;
        const uint16_t leap_date[] = {2020,2,29,12,34,56,789,6};
        for (size_t i = 0; i < 8; ++i) { uint16_t word = __builtin_bswap16(leap_date[i]); memcpy(base + date_fields + 2*i, &word, 2); }
        ctx.r3.u32 = date_fields; ctx.r4.u32 = file_time; call("RtlTimeFieldsToTime", ctx, base); require(ctx.r3.u32 == 1, "Valid leap day rejected");
        ctx.r3.u32 = file_time; ctx.r4.u32 = recovered; call("RtlTimeToTimeFields", ctx, base);
        require(!memcmp(base + date_fields, base + recovered, 16), "UTC time-fields round trip mismatch");
        uint16_t non_leap = __builtin_bswap16(uint16_t(2021)); memcpy(base + date_fields, &non_leap, 2);
        ctx.r3.u32 = date_fields; ctx.r4.u32 = file_time; call("RtlTimeFieldsToTime", ctx, base); require(ctx.r3.u32 == 0, "Invalid leap day accepted");
        write32(base, date_fields, 0xDEADBEEF);
        ctx.r3.u32 = 3; ctx.r4.u32 = 9; ctx.r5.u32 = date_fields; ctx.r6.u32 = 2; ctx.r7.u32 = date_fields + 8;
        call("ExGetXConfigSetting", ctx, base); require(ctx.r3.u32 == 0xC0000023 && read32(base, date_fields) == 0xDEADBEEF, "Small XConfig buffer was overwritten");
        ctx.r3.u32 = 3; ctx.r4.u32 = 9; ctx.r5.u32 = date_fields; ctx.r6.u32 = 4;
        call("ExGetXConfigSetting", ctx, base); require(ctx.r3.u32 == 0 && read32(base, date_fields) == 1, "English language setting mismatch");

        LARGE_INTEGER before{}, after{}, frequency{};
        QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&before);
        const auto ticks = gh2_read_timebase(); QueryPerformanceCounter(&after);
        auto scaled = [&](uint64_t count) { return uint64_t(static_cast<unsigned __int128>(count) * gh2_timebase_frequency / uint64_t(frequency.QuadPart)); };
        require(ticks >= scaled(before.QuadPart) && ticks <= scaled(after.QuadPart), "Timebase disagrees with performance clock");
        call("KeQueryPerformanceFrequency", ctx, base); require(ctx.r3.u32 == gh2_timebase_frequency, "Reported timebase frequency mismatch");
        ctx.r3.u32 = 0x103; call("RtlNtStatusToDosError", ctx, base); require(ctx.r3.u32 == 997, "STATUS_PENDING conversion mismatch");
        const uint32_t physical_flags[] = {0x404, 0x20000404, 0x80000404};
        memory.commit(0x7F001000, 0x1000);
        write32(base, 0x7F001040, 0xAABBCCDD);
        require(read32(base, 0xA0001040) == 0xAABBCCDD && read32(base, 0xC0001040) == 0xAABBCCDD && read32(base, 0xE0000040) == 0xAABBCCDD, "GPU writeback aliases disagree");
        write32(base, 0xE0000040, 0x12345678);
        require(read32(base, 0x7F001040) == 0x12345678, "GPU writeback alias is not bidirectional");
        ctx.r3.u32 = 0x7F001040; call("MmGetPhysicalAddress", ctx, base);
        require(ctx.r3.u32 == 0x1040, "Writeback physical-address conversion failed");
        const uint32_t page_sizes[] = {0x1000, 0x10000, 0x1000000};
        uint32_t physical_blocks[3];
        for (unsigned i = 0; i < 3; ++i) {
            ctx.r3.u32 = 0; ctx.r4.u32 = 0x12C0; ctx.r5.u32 = physical_flags[i];
            ctx.r6.u32 = 0; ctx.r7.u32 = UINT32_MAX; ctx.r8.u32 = 0;
            call("MmAllocatePhysicalMemoryEx", ctx, base);
            const uint32_t address = ctx.r3.u32; physical_blocks[i] = address;
            require(address && !(address & (page_sizes[i] - 1)), "Physical virtual alignment failed");
            call("MmGetPhysicalAddress", ctx, base); const uint32_t physical = ctx.r3.u32;
            const uint32_t aliases[] = {0xA0000000 + physical, 0xC0000000 + physical, 0xE0000000 + physical - 0x1000};
            for (unsigned alias = 0; alias < 3; ++alias) {
                write32(base, aliases[alias] + 12, 0xC0FFEE00 + alias);
                for (auto other : aliases) require(read32(base, other + 12) == 0xC0FFEE00 + alias, "Physical aliases do not share bytes");
            }
            ctx.r3.u32 = address + 12; call("MmQueryAllocationSize", ctx, base);
            require(ctx.r3.u32 == (i == 0 ? 0x2000 : page_sizes[i]), "Physical allocation size rounding failed");
            ctx.r3.u32 = address; call("MmQueryAddressProtect", ctx, base);
            require(ctx.r3.u32 == physical_flags[i], "Physical protection metadata failed");
        }
        for (auto address : physical_blocks) {
            ctx.r3.u32 = 0; ctx.r4.u32 = address; call("MmFreePhysicalMemory", ctx, base);
            MEMORY_BASIC_INFORMATION info{}; VirtualQuery(base + address, &info, sizeof(info));
            require(info.Protect == PAGE_NOACCESS, "Freed physical allocation remains accessible");
        }
        ctx.r3.u32 = 0; ctx.r4.u32 = 0x2000; ctx.r5.u32 = 0x404; ctx.r6.u32 = 0x1000000; ctx.r7.u32 = 0x1003FFF; ctx.r8.u32 = 0x1000;
        call("MmAllocatePhysicalMemoryEx", ctx, base); require(ctx.r3.u32 == physical_blocks[0], "Freed physical extent was not reused");
        require(read32(base, ctx.r3.u32 + 12) == 0, "Reused physical memory was not initialized");
        ctx.r4.u32 = ctx.r3.u32; ctx.r3.u32 = 0; call("MmFreePhysicalMemory", ctx, base);
        ctx.r3.u32 = 0; ctx.r4.u32 = 0x8000; ctx.r5.u32 = 0x404; ctx.r6.u32 = 0x1000000; ctx.r7.u32 = 0x1003FFF; ctx.r8.u32 = 0;
        call("MmAllocatePhysicalMemoryEx", ctx, base); require(ctx.r3.u32 == 0, "Physical range bounds were ignored");
        std::cout << "Physical aliases, three page sizes, bounds, release protection and reuse passed.\n";
        memory.commit(uint32_t(PPC_IMAGE_BASE + PPC_IMAGE_SIZE), uint32_t(PPC_CODE_SIZE * 2 + 16));
        PPC_LOOKUP_FUNC(base, PPC_CODE_BASE) = worker_fixture;
        ctx.r3.u32 = 0x70003000; call("RtlInitializeCriticalSection", ctx, base);
        uint32_t workers[2];
        write32(base, 0x70003100, 0);
        for (uint32_t i = 0; i < 2; ++i) {
            ctx.r3.u32 = 0x70003200; ctx.r4.u32 = 0x4000; ctx.r5.u32 = 0;
            ctx.r6.u32 = 0; ctx.r7.u32 = PPC_CODE_BASE; ctx.r8.u32 = i + 100; ctx.r9.u32 = 1;
            call("ExCreateThread", ctx, base); require(ctx.r3.u32 == 0, "Worker creation failed");
            workers[i] = read32(base, 0x70003200);
        }
        Sleep(10); require(read32(base, 0x70003100) == 0, "Suspended worker ran prematurely");
        for (auto handle : workers) { ctx.r3.u32 = handle; ctx.r4.u32 = 0; call("NtResumeThread", ctx, base); require(ctx.r3.u32 == 0, "Resume failed"); }
        write32(base, 0x70003210, 0xFFFFFFFF); write32(base, 0x70003214, uint32_t(-100000000));
        for (auto handle : workers) {
            ctx.r3.u32 = handle; ctx.r4.u32 = 0; ctx.r5.u32 = 0; ctx.r6.u32 = 0x70003210;
            call("NtWaitForSingleObjectEx", ctx, base); require(ctx.r3.u32 == 0, "Worker join timed out");
            ctx.r3.u32 = handle; call("NtClose", ctx, base); require(ctx.r3.u32 == 0, "Worker close failed");
        }
        require(read32(base, 0x70003100) == 2000, "Concurrent critical-section increments lost");
        std::cout << "Worker creation, suspended start, resume, isolated TLS, lock contention, join and close passed.\n";
        shutdown();
        std::cout << "Loader, TLS, large-page heap, locks, disc reads/EOF, events, semaphores, path isolation, UTC dates, and clock regressions passed.\n";
    } catch (const std::exception& e) { std::cerr << "Kernel regression failed: " << e.what() << '\n'; return 1; }
}
