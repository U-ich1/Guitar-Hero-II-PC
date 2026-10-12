// Official DLSS SDK support/dimension query only. No game or reconstruction.
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <sl.h>
#include <sl_dlss.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <array>
#include "dlss_reconstruction.h"

template<class T>T* Load(HMODULE dll,const char* name){auto p=reinterpret_cast<T*>(GetProcAddress(dll,name));if(!p)throw std::runtime_error(name);return p;}
int main(int argc,char** argv) {try {
    if(argc<3||argc>4)throw std::runtime_error("Expected official SDK DLL directory, log directory and optional --evaluate");
    auto plugin=std::filesystem::u8path(argv[1]);auto logs=std::filesystem::u8path(argv[2]);
    std::filesystem::create_directories(logs);
    const auto dll=LoadLibraryExW((plugin/L"sl.interposer.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!dll)throw std::runtime_error("Cannot load official Streamline interposer");
    auto init=Load<PFun_slInit>(dll,"slInit");auto shutdown=Load<PFun_slShutdown>(dll,"slShutdown");
    auto supported=Load<PFun_slIsFeatureSupported>(dll,"slIsFeatureSupported");
    auto deviceSet=Load<PFun_slSetD3DDevice>(dll,"slSetD3DDevice");
    auto getFunction=Load<PFun_slGetFeatureFunction>(dll,"slGetFeatureFunction");
    sl::Preferences prefs;const wchar_t* paths[]={plugin.c_str()};
    const sl::Feature features[]={sl::kFeatureDLSS};
    prefs.pathsToPlugins=paths;prefs.numPathsToPlugins=1;prefs.pathToLogsAndData=logs.c_str();
    prefs.featuresToLoad=features;prefs.numFeaturesToLoad=1;
    prefs.engine=sl::EngineType::eCustom;prefs.engineVersion="ReXGlue-c94f5ebd-GH2";
    prefs.projectId="b97ad29a-43b7-4e20-a6dd-e4de9e80f879";
    prefs.flags=sl::PreferenceFlags::eUseManualHooking|sl::PreferenceFlags::eDisableCLStateTracking|sl::PreferenceFlags::eUseFrameBasedResourceTagging;
    const auto initResult=init(prefs,sl::kSDKVersion);
    std::cout<<"{\"sdk\":\"2.14.1\",\"init\":"<<int(initResult);
    if(initResult!=sl::Result::eOk){std::cout<<",\"reconstruction_tested\":false}\n";return 2;}
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;Microsoft::WRL::ComPtr<ID3D12Device> device;
    // Streamline must shut down while its D3D12 device is still alive.
    struct Guard{PFun_slShutdown* shutdown;~Guard(){shutdown();}}guard{shutdown};
    if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))throw std::runtime_error("DXGI factory failed");
    DXGI_ADAPTER_DESC1 desc{};sl::Result supportResult=sl::Result::eErrorNoSupportedAdapterFound;
    for(UINT i=0;factory->EnumAdapters1(i,&adapter)!=DXGI_ERROR_NOT_FOUND;++i){
        adapter->GetDesc1(&desc);sl::AdapterInfo info;
        info.deviceLUID=reinterpret_cast<uint8_t*>(&desc.AdapterLuid);info.deviceLUIDSizeInBytes=sizeof(LUID);
        supportResult=supported(sl::kFeatureDLSS,info);
        if(supportResult==sl::Result::eOk && SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device))))break;
        adapter.Reset();
    }
    std::cout<<",\"support\":"<<int(supportResult)<<",\"vendor\":"<<desc.VendorId;
    if(!device){std::cout<<",\"reconstruction_tested\":false}\n";return 2;}
    const auto setResult=deviceSet(device.Get());std::cout<<",\"device_set\":"<<int(setResult);
    void* function=nullptr;auto getResult=getFunction(sl::kFeatureDLSS,"slDLSSGetOptimalSettings",function);
    std::cout<<",\"settings_function\":"<<int(getResult)<<",\"modes\":[";
    if(setResult!=sl::Result::eOk||getResult!=sl::Result::eOk){std::cout<<"],\"reconstruction_tested\":false}\n";return 2;}
    auto optimal=reinterpret_cast<PFun_slDLSSGetOptimalSettings*>(function);
    const std::array<std::pair<const char*,sl::DLSSMode>,4> modes={{{"performance",sl::DLSSMode::eMaxPerformance},{"balanced",sl::DLSSMode::eBalanced},{"quality",sl::DLSSMode::eMaxQuality},{"native_aa",sl::DLSSMode::eDLAA}}};
    bool first=true;int failures=0;
    for(auto output:std::array<std::pair<uint32_t,uint32_t>,3>{{{1920,1080},{2560,1440},{3840,2160}}})for(auto mode:modes){
        sl::DLSSOptions options;options.mode=mode.second;options.outputWidth=output.first;options.outputHeight=output.second;
        options.colorBuffersHDR=sl::Boolean::eFalse;options.useAutoExposure=sl::Boolean::eTrue;
        options.qualityPreset=options.balancedPreset=options.dlaaPreset=sl::DLSSPreset::ePresetK;
        options.performancePreset=sl::DLSSPreset::ePresetM;
        sl::DLSSOptimalSettings settings;auto result=optimal(options,settings);failures+=result!=sl::Result::eOk;
        if(!first)std::cout<<',';first=false;
        std::cout<<"{\"mode\":\""<<mode.first<<"\",\"output\":["<<output.first<<','<<output.second
            <<"],\"result\":"<<int(result)<<",\"input\":["<<settings.optimalRenderWidth<<','<<settings.optimalRenderHeight<<"]}";
    }
    std::cout<<"],\"synthetic_reconstruction\":[";
    if(argc==4){
        auto results=TestDlssReconstruction(dll,device.Get());bool first=true;
        for(auto result:results){if(!first)std::cout<<',';first=false;std::cout<<"{\"mode\":\""<<result.mode<<"\",\"result\":"<<result.result<<",\"input\":["<<result.width<<','<<result.height<<"],\"output_verified\":"<<(result.output_written?"true":"false")<<'}';failures+=!result.output_written;}
    }
    std::cout<<"],\"game_reconstruction_tested\":false}\n";return failures?2:0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
