#pragma once
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <windows.h>
#include <shlobj.h>

namespace gh2 {
// The old SDK writer emitted a raw Windows log path inside TOML double
// quotes. Repair only that recognizable line, retaining every preference.
inline bool repair_legacy_config_path(const std::filesystem::path& config) {
    if(!std::filesystem::is_regular_file(config))return false;
    std::ifstream input(config,std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(input)),{});input.close();
    const std::string prefix="log_file = \"";
    size_t line=content.find(prefix);
    if(line==std::string::npos||(line>0&&content[line-1]!='\n'))return false;
    const size_t first=line+prefix.size(),newline=content.find('\n',first);
    const size_t last=content.rfind('"',newline==std::string::npos?content.size():newline);
    if(last==std::string::npos||last<=first)return false;
    const auto path=content.substr(first,last-first);
    if(path.size()<4||path[1]!=':'||path[2]!='\\'||path[3]=='\\')return false;
    std::string escaped;
    for(char character:path){if(character=='\\'||character=='"')escaped+='\\';escaped+=character;}
    content.replace(first,last-first,escaped);
    std::filesystem::copy_file(config,config.string()+".before-path-fix",std::filesystem::copy_options::skip_existing);
    const std::filesystem::path temporary=config.string()+".repair.tmp";
    {std::ofstream output(temporary,std::ios::binary);if(!output||!(output<<content))throw std::runtime_error("Could not repair GH2 settings path");}
    if(!MoveFileExW(temporary.c_str(),config.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Could not install repaired GH2 settings");
    return true;
}
inline std::filesystem::path documents_root() {
    PWSTR value = nullptr;
    const auto status = SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, nullptr, &value);
    if (FAILED(status) || !value) throw std::runtime_error("Windows Documents folder could not be located");
    const std::filesystem::path documents(value);
    CoTaskMemFree(value);
    if (documents.empty()) throw std::runtime_error("Windows Documents folder is empty");
    return documents / "GH2PC";
}

// Copy legacy user data only on first use. Documents becomes authoritative;
// existing destination files are never replaced and the old copy is retained.
inline size_t migrate_legacy_data(const std::filesystem::path& legacy_user,
                                 const std::filesystem::path& legacy_config,
                                 const std::filesystem::path& destination) {
    std::filesystem::create_directories(destination);
    const auto marker = destination / ".legacy-import-complete";
    if (std::filesystem::exists(marker)) return 0;
    const auto saves = destination / "Saves";
    std::filesystem::create_directories(saves);
    size_t copied = 0;
    if (std::filesystem::is_directory(legacy_user)) {
        for (const auto& item : std::filesystem::recursive_directory_iterator(legacy_user)) {
            if (item.is_symlink()) throw std::runtime_error("Legacy user data contains an unsupported symbolic link");
            if (!item.is_regular_file()) continue;
            const auto relative = item.path().lexically_relative(legacy_user);
            const auto target = saves / relative;
            std::filesystem::create_directories(target.parent_path());
            if (std::filesystem::copy_file(item.path(), target, std::filesystem::copy_options::skip_existing)) ++copied;
        }
    }
    if (std::filesystem::is_regular_file(legacy_config) &&
        std::filesystem::copy_file(legacy_config, destination / "gh2.toml", std::filesystem::copy_options::skip_existing)) ++copied;
    std::ofstream done(marker, std::ios::binary);
    if (!done || !(done << "Legacy data copied without replacing existing files. Original files retained.\n"))
        throw std::runtime_error("Could not record save migration completion");
    return copied;
}
}
