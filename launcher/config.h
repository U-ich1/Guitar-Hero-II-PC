#pragma once
#include "../runtime/user_paths.h"
#include <toml.hpp>
#include <sstream>
#include <variant>
#include <map>

namespace gh2::launcher {
using Value=std::variant<int64_t,bool,std::string>;
inline toml::table load(const std::filesystem::path& file){
    if(!std::filesystem::exists(file))return {};
    std::ifstream input(file,std::ios::binary);
    if(!input)throw std::runtime_error("Could not read the settings file.");
    return toml::parse(input);
}
inline Value value(const toml::table& config,const std::string& key,const Value& fallback){
    if(std::holds_alternative<int64_t>(fallback))return config[key].value_or(std::get<int64_t>(fallback));
    if(std::holds_alternative<bool>(fallback))return config[key].value_or(std::get<bool>(fallback));
    return config[key].value_or(std::get<std::string>(fallback));
}
inline void put(toml::table& config,const std::string& key,const Value& value){
    std::visit([&](const auto& item){config.insert_or_assign(key,item);},value);
}
inline void save(const std::filesystem::path& file,const std::map<std::string,Value>& changes){
    // Merge only edited keys into a fresh read, retaining advanced settings.
    auto config=load(file);
    for(const auto& [key,value]:changes)put(config,key,value);
    // Retire legacy upscalers when saving, while retaining unrelated options.
    put(config,"present_effect",std::string("bilinear"));
    for(const auto* key:{"present_cas_additional_sharpness","present_fsr_max_upsampling_passes","present_fsr_sharpness_reduction","present_fsr_quality_mode"})config.erase(key);
    if(changes.contains("resolution_scale")){
        put(config,"draw_resolution_scale_x",changes.at("resolution_scale"));
        put(config,"draw_resolution_scale_y",changes.at("resolution_scale"));
    }
    if(changes.contains("player_one_device_key"))put(config,"player_one_device",int64_t(0));
    // These are the normal PC keyboard defaults, not overrides of user choices.
    for(const auto& [key,setting]:std::map<std::string,Value>{{"mnk_mode",true},{"input_backend",std::string("xinput")},
        {"keybind_dpad_up",std::string("Up")},{"keybind_dpad_down",std::string("Down")},
        {"keybind_dpad_left",std::string("Left")},{"keybind_dpad_right",std::string("Right")}})
        if(!config.contains(key))put(config,key,setting);
    std::filesystem::create_directories(file.parent_path());
    if(std::filesystem::exists(file))std::filesystem::copy_file(file,file.wstring()+L".launcher-backup",std::filesystem::copy_options::overwrite_existing);
    const std::filesystem::path temporary=file.wstring()+L".launcher.tmp";
    {std::ofstream output(temporary,std::ios::binary);output<<"# GH2PC settings - shared by the launcher and game\n"<<config;output.flush();
        if(!output)throw std::runtime_error("Could not save settings. Your previous file was retained.");}
    if(!MoveFileExW(temporary.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Could not replace the settings file. Your previous file was retained.");
}
}
