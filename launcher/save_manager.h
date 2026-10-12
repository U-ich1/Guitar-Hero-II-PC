#pragma once
#include <filesystem>
#include <fstream>
#include <chrono>
#include <vector>
#include <stdexcept>
namespace gh2::launcher {
namespace fs=std::filesystem;
inline std::wstring save_stamp(){return std::to_wstring(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());}
inline std::vector<fs::path> save_files(const fs::path& root){
    std::vector<fs::path> result;if(!fs::exists(root))return result;
    for(const auto& entry:fs::recursive_directory_iterator(root)){
        if(fs::is_symlink(entry.symlink_status()))throw std::runtime_error("Save folders containing links are not supported.");
        if(entry.is_regular_file())result.push_back(entry.path().lexically_relative(root));
    }return result;
}
inline void copy_saves(const fs::path& source,const fs::path& destination){
    const auto origin=fs::weakly_canonical(source).native(),target_root=fs::weakly_canonical(destination).native();
    if(origin==target_root||target_root.starts_with(origin+fs::path::preferred_separator))throw std::runtime_error("Choose a destination outside the source save folder.");
    const auto files=save_files(source);if(files.empty())throw std::runtime_error("This folder contains no saves.");
    for(const auto& relative:files){auto target=destination/relative;fs::create_directories(target.parent_path());fs::copy_file(source/relative,target,fs::copy_options::overwrite_existing);}
}
inline bool valid_save_bundle(const fs::path& source){
    return fs::is_regular_file(source/"B13EBABEBABEBABE/415607E7/00000001/gh2save/gh2save.dat")&&
           fs::is_regular_file(source/"B13EBABEBABEBABE/415607E7/Headers/00000001/gh2save.header")&&
           fs::file_size(source/"B13EBABEBABEBABE/415607E7/00000001/gh2save/gh2save.dat")>0&&
           fs::file_size(source/"B13EBABEBABEBABE/415607E7/Headers/00000001/gh2save.header")>0;
}
inline fs::path backup_saves(const fs::path& user){
    const auto destination=user/"Save Backups"/save_stamp();
    if(!save_files(user/"Saves").empty())copy_saves(user/"Saves",destination);
    return destination;
}
inline void import_saves(const fs::path& source,const fs::path& user){
    if(!valid_save_bundle(source))throw std::runtime_error("Select an exported GH2PC save folder containing both the career save and its header.");
    const auto destination=fs::weakly_canonical(user/"Saves"),origin=fs::weakly_canonical(source);
    if(origin==destination)throw std::runtime_error("This is already your active save folder.");
    auto a=origin.native(),b=destination.native();
    if(a.starts_with(b+fs::path::preferred_separator)||b.starts_with(a+fs::path::preferred_separator))throw std::runtime_error("Choose a folder outside the active Saves directory.");
    // Validate the entire source before modifying active saves; preserve a backup.
    const auto files=save_files(source);for(const auto& relative:files){
        const auto name=relative.generic_string();
        if(name!="B13EBABEBABEBABE/415607E7/00000001/gh2save/gh2save.dat"&&name!="B13EBABEBABEBABE/415607E7/Headers/00000001/gh2save.header"&&name!="achievements/415607E7.toml")throw std::runtime_error("The selected folder contains files outside the supported GH2 save bundle.");
    }
    const auto stamp=save_stamp();const auto stage=user/"Save Backups"/(L"import-"+stamp),backup=user/"Save Backups"/(L"before-import-"+stamp);
    if(!save_files(destination).empty())copy_saves(destination,stage);
    copy_saves(source,stage);
    bool moved=false;
    try{if(fs::exists(destination)){fs::rename(destination,backup);moved=true;}fs::rename(stage,destination);}
    catch(...){if(moved&&!fs::exists(destination))fs::rename(backup,destination);throw;}
}
inline fs::path delete_saves(const fs::path& user){
    const auto active=fs::weakly_canonical(user/"Saves");
    if(active.parent_path()!=fs::weakly_canonical(user))throw std::runtime_error("Save path is outside the game folder.");
    if(save_files(active).empty())throw std::runtime_error("There are no saves to delete.");
    const auto backup=user/"Save Backups"/(L"deleted-"+save_stamp());fs::create_directories(backup.parent_path());
    fs::rename(active,backup);fs::create_directory(active);return backup;
}
}
