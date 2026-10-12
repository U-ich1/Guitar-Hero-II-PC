#include "../runtime/user_paths.h"
#include <iostream>
int main(){
    try{
        const std::filesystem::path file="build/config-repair-test/gh2.toml";
        std::filesystem::create_directories(file.parent_path());
        const std::string original="resolution_scale = 2\nlog_file = \"C:\\Users\\Example User\\GH2PC\\gh2.log\"\nswap_post_effect = \"fxaa_extreme\"\ngh2_display_vsync = true\n";
        {std::ofstream output(file,std::ios::binary);output<<original;}
        if(!gh2::repair_legacy_config_path(file)||gh2::repair_legacy_config_path(file))throw std::runtime_error("Repair failed or was not idempotent");
        std::ifstream backup(file.string()+".before-path-fix",std::ios::binary);
        const std::string retained((std::istreambuf_iterator<char>(backup)),{});
        if(retained!=original)throw std::runtime_error("Original settings backup differs");
        std::cout<<"PASS: precise path repair, retained original backup and repeat-launch idempotence.\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
