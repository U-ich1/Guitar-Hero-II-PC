#include <windows.h>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
namespace fs=std::filesystem;
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
    try{
        wchar_t module[32768]{};
        if(!GetModuleFileNameW(nullptr,module,32768))throw std::runtime_error("Cannot locate the updater.");
        const fs::path root=fs::path(module).parent_path();
        if(!fs::exists(root/L"Update.ps1"))throw std::runtime_error("Place Guitar Hero 2 Updater.exe beside Build.ps1 and Update.ps1 in your existing project folder.");
        fs::path python=root/L"tools/python-env/Scripts/pythonw.exe";
        if(!fs::exists(python)){
            wchar_t found[32768]{};
            if(!SearchPathW(nullptr,L"pythonw.exe",nullptr,32768,found,nullptr))throw std::runtime_error("Python was not found. Build the project first using the README instructions.");
            python=found;
        }
        HRSRC resource=FindResourceW(instance,MAKEINTRESOURCEW(103),RT_RCDATA);
        if(!resource)throw std::runtime_error("The updater program is missing.");
        HGLOBAL loaded=LoadResource(instance,resource);const auto size=SizeofResource(instance,resource);
        const auto bytes=static_cast<const char*>(LockResource(loaded));
        if(!bytes||!size)throw std::runtime_error("Cannot load the updater program.");
        const auto directory=root/L"build/updater";fs::create_directories(directory);
        const auto script=directory/(L"bootstrap-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".py");
        {std::ofstream output(script,std::ios::binary);output.write(bytes,size);if(!output)throw std::runtime_error("Cannot write the updater program to the project folder.");}
        // Run a snapshot so source installation cannot replace the executing script.
        std::wstring command=L"\""+python.wstring()+L"\" \""+script.wstring()+L"\" --root \""+root.wstring()+L"\"";
        STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
        if(!CreateProcessW(python.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,root.c_str(),&startup,&process))throw std::runtime_error("Windows could not start the updater.");
        CloseHandle(process.hThread);CloseHandle(process.hProcess);return 0;
    }catch(const std::exception& error){MessageBoxA(nullptr,error.what(),"Guitar Hero 2 Updater",MB_OK|MB_ICONERROR);return 1;}
}
