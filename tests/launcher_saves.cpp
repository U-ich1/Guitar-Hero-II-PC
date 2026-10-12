#include "../launcher/save_manager.h"
#include <iostream>
using namespace gh2::launcher;
static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void write(const fs::path& path,const std::string& value){fs::create_directories(path.parent_path());std::ofstream file(path);file<<value;}
static std::string read(const fs::path& path){std::ifstream f(path);return {std::istreambuf_iterator<char>(f),{}};}
int main(){try{
    const auto root=fs::absolute("build/launcher-save-tests")/save_stamp(),user=root/"user",bundle=root/"exported";
    const fs::path career="B13EBABEBABEBABE/415607E7/00000001/gh2save/gh2save.dat",header="B13EBABEBABEBABE/415607E7/Headers/00000001/gh2save.header",achievements="achievements/415607E7.toml";
    write(user/"Saves"/career,"old career");write(user/"Saves"/header,"old header");write(user/"Saves"/achievements,"old achievements");
    copy_saves(user/"Saves",bundle);require(valid_save_bundle(bundle),"Export is not importable");
    write(bundle/career,"new career");write(bundle/header,"new header");write(bundle/achievements,"new achievements");
    import_saves(bundle,user);require(read(user/"Saves"/career)=="new career"&&read(user/"Saves"/header)=="new header"&&read(user/"Saves"/achievements)=="new achievements","Import did not replace the complete bundle");
    bool old_kept=false;for(auto& entry:fs::directory_iterator(user/"Save Backups"))if(read(entry.path()/career)=="old career"&&read(entry.path()/header)=="old header")old_kept=true;require(old_kept,"Previous save not backed up");
    write(bundle/"unexpected.exe","reject me");bool rejected=false;try{import_saves(bundle,user);}catch(...){rejected=true;}require(rejected&&read(user/"Saves"/career)=="new career","Invalid bundle changed live saves");
    auto backup=delete_saves(user);require(save_files(user/"Saves").empty()&&read(backup/career)=="new career","Deletion not recoverable");
    import_saves(backup,user);require(read(user/"Saves"/career)=="new career","Deleted save could not be restored");
    rejected=false;try{import_saves(user/"Saves",user);}catch(...){rejected=true;}require(rejected,"Self import accepted");
    std::cout<<"PASS: complete export/import, backups, invalid-source rejection, recoverable deletion and restore. Workspace fixtures only.\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
