#include <rex/runtime.h>
#include <rex/kernel/init.h>
#include <rex/system/xam/content_manager.h>
#include <rex/filesystem/file.h>
#include <rex/filesystem/entry.h>
#include <rex/logging.h>
#include "../runtime/user_paths.h"
#include <iostream>
#include <array>
#include <vector>

LONG CALLBACK save_test_fault(EXCEPTION_POINTERS* info) {
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
    HMODULE module{};
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(info->ContextRecord->Rip), &module);
    char name[MAX_PATH]{};
    GetModuleFileNameA(module, name, MAX_PATH);
    std::cerr << "Save probe fault: " << name << " +0x" << std::hex
              << info->ContextRecord->Rip - uintptr_t(module) << ", address 0x"
              << info->ExceptionRecord->ExceptionInformation[1] << '\n' << std::flush;
    return EXCEPTION_CONTINUE_SEARCH;
}

int main(int argc, char** argv) {
    try {
        AddVectoredExceptionHandler(0, save_test_fault);
        if (argc == 2 && std::string(argv[1]) == "--migrate") {
            const auto root = gh2::documents_root();
            const auto count = gh2::migrate_legacy_data("build/windowed-state/user", "build/windowed-state/gh2.toml", root);
            std::cout << "Documents root: " << root.string() << "\nCopied " << count << " legacy files; originals retained.\n";
            return 0;
        }
        if (argc != 3) return 1;
        const bool create = std::string(argv[1]) == "--create";
        const bool existing = std::string(argv[1]) == "--read-existing";
        const std::filesystem::path root(argv[2]);
        std::filesystem::create_directories(root);
        rex::InitLogging("build/save_runtime.log", spdlog::level::debug);
        struct LoggingGuard { ~LoggingGuard() { rex::ShutdownLogging(); } } logging_guard;
        rex::Runtime runtime("private", root);
        rex::RuntimeConfig config;
        config.tool_mode = true;
        config.kernel_init = rex::kernel::InitializeKernel;
        if (runtime.Setup(std::move(config)) != 0) throw std::runtime_error("Save runtime setup failed");
        if (runtime.LoadXexImage("game:\\default.xex") != 0) throw std::runtime_error("Save title metadata load failed");
        auto* manager = runtime.kernel_state()->content_manager();
        rex::system::xam::XCONTENT_AGGREGATE_DATA data{};
        data.device_id = 1;
        data.content_type = rex::system::XContentType::kSavedGame;
        data.title_id = 0x415607E7;
        data.xuid = 0xB13EBABEBABEBABEULL;
        data.set_file_name(existing ? "gh2save" : "gh2pc_storage_probe");
        data.set_display_name(u"GH2PC persistence probe");
        constexpr uint64_t owner = 0xB13EBABEBABEBABEULL;
        uint32_t license = 0;
        const auto result = create ? manager->CreateContent("GH2TEST", owner, data)
                                   : manager->OpenContent("GH2TEST", owner, data, license);
        if (result != 0) throw std::runtime_error("Content package create/open failed");
        rex::filesystem::File* file = nullptr;
        rex::filesystem::FileAction action{};
        const std::string path = existing ? "GH2TEST:\\gh2save.dat" : "GH2TEST:\\marker.bin";
        const auto status = runtime.file_system()->OpenFile(nullptr, path,
            create ? rex::filesystem::FileDisposition::kCreate : rex::filesystem::FileDisposition::kOpen,
            create ? rex::filesystem::FileAccess::kGenericWrite : rex::filesystem::FileAccess::kGenericRead,
            false, true, &file, &action);
        if (status != 0 || !file) throw std::runtime_error("Guest save path open failed");
        struct FileGuard { rex::filesystem::File* file; ~FileGuard() { if (file) file->Destroy(); } } file_guard{file};
        const std::array<uint8_t,8> expected{0x47,0x48,0x32,0x50,0x43,0x00,0xA5,0x5A};
        size_t count = 0;
        if (create) {
            if (file->WriteSync(expected, 0, &count) != 0 || count != expected.size())
                throw std::runtime_error("Guest save write failed");
        } else if (existing) {
            std::vector<uint8_t> bytes(file->entry()->size());
            if (bytes.empty() || file->ReadSync(bytes, 0, &count) != 0 || count != bytes.size())
                throw std::runtime_error("Migrated GH2 save read failed");
            std::cout << "Migrated GH2 save reopened through guest VFS: " << count << " bytes.\n";
        } else {
            std::array<uint8_t,8> actual{};
            if (file->ReadSync(actual, 0, &count) != 0 || actual != expected || count != expected.size())
                throw std::runtime_error("Saved data did not survive fresh-process reopen");
        }
        file->Destroy(); file_guard.file = nullptr;
        if (manager->CloseContent("gh2test") != 0) throw std::runtime_error("Case-insensitive package close failed");
        runtime.Shutdown();
        std::cout << "PASS: " << argv[1] << " through SDK save package and guest filesystem.\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
}
