#pragma once
#include <windows.h>
#include <cstdint>
#include <stdexcept>
#include <vector>

// Private CPU address space plus shared physical aliases. The 4 KB alias
// starts at physical +0x1000; placeholders allow this page-aligned offset.
class GuestAddressSpace {
    using Alloc2 = PVOID (WINAPI*)(HANDLE,PVOID,SIZE_T,ULONG,ULONG,void*,ULONG);
    using Map3 = PVOID (WINAPI*)(HANDLE,HANDLE,PVOID,ULONG64,SIZE_T,ULONG,ULONG,void*,ULONG);
    HANDLE section = nullptr;
    struct Region { uint64_t offset, size; bool mapped = false; };
    std::vector<Region> regions;
    void cleanup() {
        for (auto r : regions) {
            if (r.mapped) UnmapViewOfFile(base + r.offset);
            else VirtualFree(base + r.offset, 0, MEM_RELEASE);
        }
        regions.clear();
        if (section) CloseHandle(section);
        section = nullptr; base = nullptr;
    }
public:
    uint8_t* base = nullptr;
    GuestAddressSpace() {
        auto module = GetModuleHandleW(L"kernelbase.dll");
        auto alloc = reinterpret_cast<Alloc2>(GetProcAddress(module, "VirtualAlloc2"));
        auto map = reinterpret_cast<Map3>(GetProcAddress(module, "MapViewOfFile3"));
        if (!alloc || !map) throw std::runtime_error("Guest aliases require Windows placeholder memory APIs");
        base = static_cast<uint8_t*>(alloc(GetCurrentProcess(), nullptr, 0x100000000ull, MEM_RESERVE | 0x40000, PAGE_NOACCESS, nullptr, 0));
        if (!base) throw std::runtime_error("Guest address reservation failed");
        regions.push_back({0, 0x100000000ull});
        try {
            const uint64_t sizes[] = {0x7F000000, 0x1000000, 0x20000000, 0x20000000, 0x20000000};
            for (auto size : sizes) {
                auto tail = regions.back();
                if (!VirtualFree(base + tail.offset, size, MEM_RELEASE | 2)) throw std::runtime_error("Guest placeholder split failed");
                regions.back().size = size;
                regions.push_back({tail.offset + size, tail.size - size});
            }
            section = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE | SEC_RESERVE, 0, 0x20001000, nullptr);
            if (!section) throw std::runtime_error("Physical backing section creation failed");
            for (size_t i = 0; i < regions.size(); ++i) {
                auto& r = regions[i];
                if (r.offset == 0 || r.offset == 0x80000000) {
                    if (!alloc(GetCurrentProcess(), base + r.offset, r.size, MEM_RESERVE | 0x4000, PAGE_READWRITE, nullptr, 0)) throw std::runtime_error("CPU address reservation failed");
                    continue;
                }
                if (!map(section, GetCurrentProcess(), base + r.offset, r.offset == 0xE0000000 ? 0x1000 : 0, r.size, 0x4000, PAGE_READWRITE, nullptr, 0)) throw std::runtime_error("Physical alias mapping failed: " + std::to_string(GetLastError()));
                r.mapped = true;
            }
        } catch (...) { cleanup(); throw; }
    }
    ~GuestAddressSpace() { cleanup(); }
    GuestAddressSpace(const GuestAddressSpace&) = delete;
    GuestAddressSpace& operator=(const GuestAddressSpace&) = delete;
    void commit(uint64_t offset, uint64_t size) {
        if (offset + size > 0x100000000ull || !VirtualAlloc(base + offset, size, MEM_COMMIT, PAGE_READWRITE)) throw std::runtime_error("Guest memory commit failed");
    }
};
