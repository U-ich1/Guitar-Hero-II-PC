#include "../runtime/graphics_dialog.h"
#include "../runtime/present_policy.h"
#include "../runtime/cheat_settings.h"
#include <rex/runtime.h>
#include <rex/system/gpu_plugin.h>
#include <rex/logging.h>
#include <dxgi.h>
#include <iostream>
#include <stdexcept>
static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static std::string unusual_text;
static auto text_registration=rex::cvar::FlagRegistrar({"gh2_test_text",rex::cvar::FlagType::String,"Test","String round trip",
    [](std::string_view value){unusual_text=value;return true;},[]{return unusual_text;},{},rex::cvar::Lifecycle::kHotReload,{},""});
static const std::string windows_path=R"(C:\Users\Example User\Documents\GH2PC\gh2.log)";
static const std::string text_fixture="quotes: \"test\"; backslash: \\; newline:\n tab:\t carriage:\r; UTF-8: guitar \xF0\x9F\x8E\xB8";
int main(int argc,char** argv) {
    try {
        const std::string mode=argc>1?argv[1]:"--settings";
        rex::InitLogging("build/graphics_runtime_test.log",spdlog::level::debug);
        struct LoggingGuard{~LoggingGuard(){rex::ShutdownLogging();}} logging_guard;
        if(mode=="--late-reload"||mode=="--startup-gpu"||mode=="--launcher-config") {
            char name[]="graphics-test";char* arguments[]={name};rex::cvar::Init(1,arguments);
            rex::cvar::LoadConfig(mode=="--launcher-config"?"build/launcher-tests/settings.toml":"build/graphics_profile_test.toml");
        }
        auto backend=rex::system::LoadGpuPlugin("xenos","d3d12");require(bool(backend),"GPU settings plugin failed to load");
        using namespace gh2::graphics;
        if(mode=="--launcher-config") {
            require(configured_scale()==3&&number("draw_resolution_scale_y")==3,"Launcher internal resolution did not reach the game");
            require(rex::cvar::GetFlagByName("swap_post_effect")=="fxaa_extreme"&&number("audio_offset_ms")==35&&gh2::cheats::hyperspeed.load()==1,"Launcher AA/audio/cheat values did not reach the game");
            require(rex::cvar::GetFlagByName("present_effect")=="bilinear","Retired upscaler was not reset");
            std::cout<<"PASS: launcher-produced TOML loads in the actual runtime with renderer, audio and Hyperspeed settings. No game launched.\n";return 0;
        }
        if(mode=="--reload"||mode=="--late-reload") {
            if(mode=="--reload")rex::cvar::LoadConfig("build/graphics_profile_test.toml");
            require(configured_scale()==2&&number("draw_resolution_scale_y")==2&&read_bool("gh2_display_vsync")&&show_fps.load(),"Graphics settings did not reload");
            require(number("window_width")==1920&&number("window_height")==1080&&rex::cvar::GetFlagByName("swap_post_effect")=="fxaa","Output/AA settings did not reload");
            require(rex::cvar::GetFlagByName("log_file")==windows_path&&unusual_text==text_fixture,"Windows path or special string did not round trip");
            require(number("gh2_display_fps_limit")==73,"Custom FPS limit did not reload");
            require(gh2::scene::no_audience.load(),"Audience setting did not reload");
            require(rex::cvar::GetFlagByName("present_effect")=="bilinear","Default display filtering did not reload");
            std::cout<<"PASS: resolution, VSync, window size, FXAA and FPS settings reload in a fresh process.\n";return 0;
        }
        if(mode=="--gpu"||mode=="--startup-gpu") {
            const int scale=mode=="--startup-gpu"?2:argc>2?std::atoi(argv[2]):2;
            if(mode=="--gpu")require(set_scale(scale),"GPU scale rejected");
            else require(configured_scale()==scale,"Saved scale did not reach plugin startup");
            rex::Runtime runtime("private","build/graphics-test-state/user",{},"build/graphics-test-state/cache");
            rex::RuntimeConfig config;config.graphics=std::move(backend);
            require(runtime.Setup(std::move(config))==0,"Scaled D3D12 backend setup failed");
            // The blocking shader-storage call is marshalled onto the GPU
            // worker after SetupContext, acting as a full initialization fence.
            runtime.graphics_system()->InitializeShaderStorage("build/graphics-test-state/cache",0,true);
            runtime.Shutdown();std::cout<<"PASS: D3D12 runtime initialized at requested "<<scale<<"x scaling; no game launched.\n";return 0;
        }
        for(bool vsync:{false,true})for(bool tearing:{false,true}) {
            const auto policy=present_policy(vsync,tearing);
            require(policy.interval==(vsync?1U:0U),"Display VSync not wired to sync interval");
            require(!vsync||!(policy.flags&DXGI_PRESENT_ALLOW_TEARING),"Invalid tearing flag with VSync");
            require(vsync||bool(policy.flags&DXGI_PRESENT_ALLOW_TEARING)==tearing,"Tearing support ignored");
        }
        require(!set_scale(0)&&!set_scale(5),"Unsupported graphics preset accepted");
        rex::cvar::SetFlagByName("draw_resolution_scale_x","3");rex::cvar::SetFlagByName("draw_resolution_scale_y","1");
        require(set_scale(2)&&number("resolution_scale")==2&&number("draw_resolution_scale_x")==2&&number("draw_resolution_scale_y")==2,"Advanced axis override defeated internal resolution preset");
        require(set_window_size(2)&&!set_window_size(5),"Window preset validation failed");
        require(rex::cvar::SetFlagByName("swap_post_effect","fxaa")&&rex::cvar::SetFlagByName("anisotropic_override","5"),"Actual AA/filtering setting rejected");
        require(!rex::cvar::SetFlagByName("present_effect","fsr")&&!rex::cvar::SetFlagByName("present_effect","cas")&&rex::cvar::GetFlagByName("present_effect")=="bilinear","Retired upscaler still selectable");
          require(rex::cvar::SetFlagByName("gh2_no_audience","true"),"Crowd setting rejected");
        require(!rex::cvar::SetFlagByName("present_effect","fsr2")&&!rex::cvar::SetFlagByName("present_effect","fsr3"),"Unverified temporal FSR enabled");
        require(rex::cvar::GetFlagInfo("present_effect")->lifecycle==rex::cvar::Lifecycle::kRequiresRestart,"Upscaling restart metadata missing");
        require(rex::cvar::SetFlagByName("gh2_display_vsync","true")&&rex::cvar::SetFlagByName("gh2_show_fps","true"),"Display toggles rejected");
        require(!rex::cvar::SetFlagByName("gh2_display_fps_limit","-1")&&!rex::cvar::SetFlagByName("gh2_display_fps_limit","60.5")&&!rex::cvar::SetFlagByName("gh2_display_fps_limit","2147483648"),"Invalid FPS cap accepted");
        require(rex::cvar::SetFlagByName("gh2_display_fps_limit","0")&&rex::cvar::SetFlagByName("gh2_display_fps_limit","73"),"Unlimited or custom integer FPS limit rejected");
        require(rex::cvar::GetFlagInfo("resolution_scale")->lifecycle==rex::cvar::Lifecycle::kRequiresRestart,"Render scale not marked restart-only");
        require(rex::cvar::SetFlagByName("log_file",windows_path)&&rex::cvar::SetFlagByName("gh2_test_text",text_fixture),"String fixture rejected");
        rex::cvar::SaveConfig("build/graphics_profile_test.toml");
        std::cout<<"PASS: DXGI presentation policy, scale/axis override, actual renderer options and restart metadata.\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
