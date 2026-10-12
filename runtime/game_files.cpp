#include "game_files.h"
#include "kernel_runtime.h"
#include <windows.h>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <sstream>

namespace gh2::files {
namespace {
constexpr uint32_t invalid_parameter = 0xC000000D, invalid_handle = 0xC0000008;
struct File { HANDLE host; std::filesystem::path path; bool directory; bool synchronous; };
std::map<uint32_t, File> handles;
std::filesystem::path root;
uint32_t next_handle = 0x100;
uint64_t bytes_read = 0;
using gh2::kernel::read32;
using gh2::kernel::write32;
uint64_t read64(uint8_t* base, uint32_t address) { return (uint64_t(read32(base, address)) << 32) | read32(base, address + 4); }
void write64(uint8_t* base, uint32_t address, uint64_t value) { write32(base, address, value >> 32); write32(base, address + 4, uint32_t(value)); }
void io_status(uint8_t* base, uint32_t address, uint32_t status, uint32_t information = 0) {
    if (address) { write32(base, address, status); write32(base, address + 4, information); }
}
uint32_t win_status(DWORD error) {
    if (error == ERROR_FILE_NOT_FOUND) return 0xC0000034;
    if (error == ERROR_PATH_NOT_FOUND) return 0xC000003A;
    if (error == ERROR_ACCESS_DENIED) return 0xC0000022;
    if (error == ERROR_SHARING_VIOLATION) return 0xC0000043;
    return 0xC0000001;
}
std::string ansi(uint8_t* base, uint32_t address) {
    uint16_t length; memcpy(&length, base + address, 2); length = __builtin_bswap16(length);
    uint32_t buffer = read32(base, address + 4);
    if (length > 4096 || (length && !buffer)) throw std::runtime_error("Invalid guest file-name string");
    return std::string(reinterpret_cast<char*>(base + buffer), length);
}
std::filesystem::path resolve(const std::string& raw, uint32_t relative_handle) {
    std::string path = raw;
    std::replace(path.begin(), path.end(), '\\', '/');
    if (path.starts_with("/?" "?/")) path.erase(0, 4);
    auto lowered = path;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    std::filesystem::path parent = root;
    bool absolute_guest = false;
    for (const std::string prefix : {"game:/", "d:/", "/device/cdrom0/"}) {
        if (lowered.starts_with(prefix)) { path.erase(0, prefix.size()); absolute_guest = true; break; }
    }
    if (lowered == "game:" || lowered == "d:" || lowered == "/device/cdrom0") { path.clear(); absolute_guest = true; }
    if (path.find(':') != std::string::npos || path.starts_with('/')) return {};
    if (!absolute_guest && relative_handle && relative_handle != 0xFFFFFFFD) {
        auto found = handles.find(relative_handle);
        if (found == handles.end() || !found->second.directory) return {};
        parent = found->second.path;
    }
    const std::filesystem::path relative(path);
    for (const auto& component : relative) if (component == "..") return {};
    // Resolve lexically, then reject every reparse-point component. This keeps
    // disc access inside the extracted tree without walking protected parents.
    const auto target = (parent / relative).lexically_normal();
    auto r = root.begin(), t = target.begin();
    for (; r != root.end(); ++r, ++t) if (t == target.end() || *r != *t) return {};
    std::filesystem::path probe = root;
    for (const auto& component : target.lexically_relative(root)) {
        probe /= component;
        const DWORD attributes = GetFileAttributesW(probe.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return {};
    }
    return target;
}
uint32_t open(uint8_t* base, PPCContext& ctx, bool simple) {
    const uint32_t output = ctx.r3.u32, access = ctx.r4.u32, attributes = ctx.r5.u32, ios = ctx.r6.u32;
    if (!output || !attributes) return invalid_parameter;
    const uint32_t disposition = simple ? 1 : ctx.r10.u32;
    const uint32_t options = simple ? ctx.r7.u32 : read32(base, ctx.r1.u32 + 0x54);
    const uint32_t string = read32(base, attributes + 4), directory_handle = read32(base, attributes);
    if (!string) return invalid_parameter;
    const auto guest_name = ansi(base, string);
    const auto target = resolve(guest_name, directory_handle);
    std::cout << "File open: " << guest_name << '\n';
    uint32_t status = 0;
    if (target.empty()) status = 0xC000003A;
    else if ((access & (0x40000000 | 2 | 4 | 0x10 | 0x100)) || (disposition != 1 && disposition != 3)) status = 0xC0000022; // Disc is read-only.
    if (status) { write32(base, output, UINT32_MAX); io_status(base, ios, status); return status; }
    const bool directory = std::filesystem::is_directory(target);
    if ((options & 1) && !directory) status = 0xC0000103;
    if ((options & 0x40) && directory) status = 0xC00000BA;
    HANDLE host = INVALID_HANDLE_VALUE;
    if (!status) {
        host = CreateFileW(target.c_str(), directory ? FILE_LIST_DIRECTORY : GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           directory ? FILE_FLAG_BACKUP_SEMANTICS : FILE_ATTRIBUTE_NORMAL, nullptr);
        if (host == INVALID_HANDLE_VALUE) status = win_status(GetLastError());
    }
    if (status) { write32(base, output, UINT32_MAX); io_status(base, ios, status); return status; }
    const uint32_t handle = next_handle++;
    handles.emplace(handle, File{host, target, directory, bool(options & 0x30)});
    write32(base, output, handle); io_status(base, ios, 0, 1); return 0;
}
}
void initialize(const std::string& path) {
    shutdown(); root = std::filesystem::absolute(path).lexically_normal();
    const DWORD attributes = GetFileAttributesW(root.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        throw std::runtime_error("Game data root must be an accessible ordinary directory");
    next_handle = 0x100; bytes_read = 0;
}
void shutdown() { for (const auto& [id, file] : handles) CloseHandle(file.host); handles.clear(); }
void report() { std::cout << "Game file bytes read: " << bytes_read << ".\n"; }
bool service(const std::string& name, PPCContext& ctx, uint8_t* base) {
    if (name == "NtCreateFile" || name == "NtOpenFile") { ctx.r3.u64 = open(base, ctx, name == "NtOpenFile"); return true; }
    if (name == "NtClose") {
        auto found = handles.find(ctx.r3.u32);
        if (found == handles.end()) return false; // Other kernel-object types handled elsewhere.
        CloseHandle(found->second.host); handles.erase(found); ctx.r3.u64 = 0; return true;
    }
    if (name == "NtWaitForSingleObjectEx" || name == "KeWaitForSingleObject") {
        // A native read completes before returning STATUS_PENDING. File handles
        // are therefore signalled when their completion is subsequently waited.
        if (!handles.contains(ctx.r3.u32)) return false;
        ctx.r3.u64 = 0; return true;
    }
    if (name != "NtReadFile" && name != "NtQueryInformationFile" && name != "NtSetInformationFile") return false;
    auto found = handles.find(ctx.r3.u32);
    if (found == handles.end()) { ctx.r3.u64 = invalid_handle; return true; }
    auto& file = found->second;
    if (name == "NtReadFile") {
        if (ctx.r5.u32 & ~1u) {
            std::ostringstream message;
            message << "NtReadFile event/APC support required: event=0x" << std::hex << ctx.r4.u32 << ", APC=0x" << ctx.r5.u32;
            throw std::runtime_error(message.str());
        }
        if (ctx.r4.u32 && !gh2::kernel::signal_event(ctx.r4.u32, true)) {
            io_status(base, ctx.r7.u32, invalid_handle); ctx.r3.u64 = invalid_handle; return true;
        }
        if (ctx.r10.u32) {
            const uint64_t offset = read64(base, ctx.r10.u32);
            if (offset != UINT64_MAX - 1) {
                LARGE_INTEGER position; position.QuadPart = offset;
                if (!SetFilePointerEx(file.host, position, nullptr, FILE_BEGIN)) { ctx.r3.u64 = invalid_parameter; return true; }
            }
        }
        DWORD actual = 0;
        uint32_t status = 0;
        if (!ReadFile(file.host, base + ctx.r8.u32, ctx.r9.u32, &actual, nullptr)) status = win_status(GetLastError());
        else if (!actual && ctx.r9.u32) status = 0xC0000011;
        io_status(base, ctx.r7.u32, status, actual); bytes_read += actual;
        if (ctx.r4.u32) gh2::kernel::signal_event(ctx.r4.u32, false);
        // This backend has completed the request and filled the IO status block
        // before returning, so the operation is no longer pending.
        ctx.r3.u64 = status; return true;
    }
    const uint32_t ios = ctx.r4.u32, output = ctx.r5.u32, length = ctx.r6.u32, info = ctx.r7.u32;
    if (name == "NtSetInformationFile") {
        if (info != 14 || length < 8) throw std::runtime_error("Unsupported NtSetInformationFile information class " + std::to_string(info));
        LARGE_INTEGER position; position.QuadPart = read64(base, output);
        uint32_t status = SetFilePointerEx(file.host, position, nullptr, FILE_BEGIN) ? 0 : invalid_parameter;
        io_status(base, ios, status); ctx.r3.u64 = status; return true;
    }
    BY_HANDLE_FILE_INFORMATION metadata{};
    if (!GetFileInformationByHandle(file.host, &metadata)) { ctx.r3.u64 = win_status(GetLastError()); return true; }
    const uint64_t size = (uint64_t(metadata.nFileSizeHigh) << 32) | metadata.nFileSizeLow;
    uint32_t required = info == 5 ? 24 : info == 14 ? 8 : info == 4 ? 40 : info == 34 ? 56 : 0;
    if (!required) throw std::runtime_error("Unsupported NtQueryInformationFile information class " + std::to_string(info));
    if (length < required) { io_status(base, ios, 0xC0000004); ctx.r3.u64 = 0xC0000004; return true; }
    memset(base + output, 0, required);
    if (info == 5) {
        write64(base, output, (size + 4095) & ~4095ull); write64(base, output + 8, size);
        write32(base, output + 16, metadata.nNumberOfLinks); base[output + 21] = file.directory;
    } else if (info == 14) {
        LARGE_INTEGER zero{}, current{}; SetFilePointerEx(file.host, zero, &current, FILE_CURRENT); write64(base, output, current.QuadPart);
    } else {
        auto time = [](FILETIME t) { return (uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
        write64(base, output, time(metadata.ftCreationTime)); write64(base, output + 8, time(metadata.ftLastAccessTime));
        write64(base, output + 16, time(metadata.ftLastWriteTime)); write64(base, output + 24, time(metadata.ftLastWriteTime));
        if (info == 34) {
            write64(base, output + 32, (size + 4095) & ~4095ull); write64(base, output + 40, size);
            write32(base, output + 48, metadata.dwFileAttributes);
        } else write32(base, output + 32, metadata.dwFileAttributes);
    }
    io_status(base, ios, 0, required); ctx.r3.u64 = 0; return true;
}
}
