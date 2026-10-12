#pragma once
#include "graphics_settings.h"
#include "game_frame_stats.h"
#include <rex/ui/imgui_dialog.h>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <filesystem>
#include <atomic>
#include <rex/logging.h>
#include "discord_presence_state.h"
namespace gh2::graphics {
inline std::atomic<bool> show_fps{false};
inline auto fps_registration=rex::cvar::FlagRegistrar({"gh2_show_fps",rex::cvar::FlagType::Boolean,"Graphics/Display","Show presentation FPS",
    [](std::string_view value){if(value!="true"&&value!="false"&&value!="1"&&value!="0")return false;show_fps.store(value=="true"||value=="1");return true;},
    []{return show_fps.load()?"true":"false";},{},rex::cvar::Lifecycle::kHotReload,{},"false"});
}
class GraphicsDialog final : public rex::ui::ImGuiDialog {
    std::filesystem::path config_;
    bool visible_=false;
    int initial_scale_,initial_width_,initial_height_,initial_monitor_;
    std::string initial_aa_;
    std::vector<std::string> monitor_names_;
    bool saved_=false;
    int fps_limit_edit_=0;
    void RefreshMonitors(){
        monitor_names_.clear();int count=0;SDL_DisplayID* displays=SDL_GetDisplays(&count);
        for(int i=0;displays&&i<count;++i){const char* name=SDL_GetDisplayName(displays[i]);monitor_names_.push_back(name?name:"Display");}
        SDL_free(displays);
    }
protected:
    bool WantsContinuousRepaint() const override { return visible_; }
    void OnDraw(ImGuiIO& io) override {
        using namespace gh2::graphics;
        // Log from the UI thread, keeping logging off the guest's VdSwap path.
        const auto now=std::chrono::steady_clock::now();
        static auto last_log=now-std::chrono::seconds(5);
        static int last_cap=-1;
        const int cap=number("gh2_display_fps_limit");
        if(cap!=last_cap||now-last_log>=std::chrono::seconds(5)){
            REXSYS_INFO("GH2 performance: game_output={:.2f}/s display_updates={:.2f}/s worst_interval={:.2f}ms cap={} display_vsync={} configured_scale={}x",
                gh2::performance::output_fps.load(),io.Framerate,
                gh2::performance::log_worst_frame_ms.exchange(0),cap,
                read_bool("gh2_display_vsync",false),configured_scale());
            const auto calls=gh2::performance::swap_calls.exchange(0),total=gh2::performance::swap_total_ns.exchange(0),maximum=gh2::performance::swap_max_ns.exchange(0);
            if(calls)REXSYS_INFO("GH2 guest swap: samples={} avg_ms={:.3f} max_ms={:.3f}",calls,double(total)/double(calls)/1000000.0,double(maximum)/1000000.0);
            const auto skipped=gh2::scene::crowd_draws_skipped.exchange(0);
            REXSYS_INFO("GH2 scene: no_audience={} crowd_draws_skipped={}",gh2::scene::no_audience.load(),skipped);
            {std::lock_guard lock(gh2::discord::snapshot_mutex);const auto& state=gh2::discord::snapshot;
                REXSYS_INFO("GH2 performance scene: status={} mode={} song={} venue={}",state.status,state.mode,state.song,state.venue);}
            last_cap=cap;last_log=now;
        }
        if(show_fps.load()) {
            ImGui::SetNextWindowBgAlpha(0.65f);ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x-15,15),ImGuiCond_Always,ImVec2(1,0));
            if(ImGui::Begin("Presentation FPS",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoInputs|ImGuiWindowFlags_NoSavedSettings)){
                ImGui::Text("Game output: %.1f/s",gh2::performance::output_fps.load());
                ImGui::Text("Display updates: %.1f/s",io.Framerate);
                ImGui::Text("Worst game interval: %.1f ms",gh2::performance::worst_frame_ms.load());
            }
            ImGui::End();
        }
        if(!visible_)return;
        ImGui::SetNextWindowSize(ImVec2(590,550),ImGuiCond_FirstUseEver);
        if(ImGui::Begin("Graphics settings (F5)",&visible_)) {
            ImGui::Text("Startup internal resolution: %dx%d (%dx)",1280*initial_scale_,720*initial_scale_,initial_scale_);
            ImGui::Text("Startup anti-aliasing: %s",initial_aa_.c_str());
            ImGui::TextWrapped("Resolution and anti-aliasing below are settings for the next launch. Save, close normally and launch again to apply them.");
            int scale=std::clamp(configured_scale(),1,4)-1;
            if(ImGui::Combo("Internal resolution",&scale,"1x - native 1280x720\0 2x - 2560x1440\0 3x - 3840x2160 (4K)\0 4x - 5120x2880\0"))set_scale(scale+1);
            ImGui::TextWrapped("Internal scaling sharpens rendered geometry. Videos and original textures retain their source detail. Higher settings use more GPU memory.");
            bool fullscreen=read_bool("fullscreen",true);
            if(ImGui::Checkbox("Borderless fullscreen",&fullscreen))rex::cvar::SetFlagByName("fullscreen",fullscreen?"true":"false");
            int size=0;for(unsigned i=0;i<window_sizes.size();++i)if(number("window_width")==window_sizes[i][0]&&number("window_height")==window_sizes[i][1])size=int(i);
            if(ImGui::Combo("Window size",&size,"1280x720\0 1600x900\0 1920x1080\0 2560x1440\0 3840x2160\0"))set_window_size(size);
            const int count=int(monitor_names_.size());
            int monitor=number("monitor");std::string label=monitor==0?"System default":"Display "+std::to_string(monitor);
            if(monitor>0&&monitor<=count)label=std::to_string(monitor)+" - "+monitor_names_[monitor-1];
            if(ImGui::BeginCombo("Monitor",label.c_str())) {
                if(ImGui::Selectable("System default",monitor==0))rex::cvar::SetFlagByName("monitor","0");
                for(int i=0;i<count;++i){const auto text=std::to_string(i+1)+" - "+monitor_names_[i];
                    if(ImGui::Selectable(text.c_str(),monitor==i+1))rex::cvar::SetFlagByName("monitor",std::to_string(i+1));}
                ImGui::EndCombo();
            }
            bool sync=read_bool("gh2_display_vsync");
            if(ImGui::Checkbox("Display VSync",&sync))rex::cvar::SetFlagByName("gh2_display_vsync",sync?"true":"false");
            ImGui::TextWrapped("VSync reduces tearing and may increase input latency. GH2's gameplay clock remains unchanged.");
            ImGui::InputInt("Display FPS limit (0 = unlimited)",&fps_limit_edit_);
            ImGui::TextWrapped("Save applies this limit without restarting. It caps displayed frames only; game timing is unchanged. VSync/monitor refresh may impose a lower rate.");
            if(fps_limit_edit_<0)ImGui::TextUnformatted("Enter zero or a positive whole number.");
            int aa=0;const auto current=rex::cvar::GetFlagByName("swap_post_effect");if(current=="fxaa")aa=1;else if(current=="fxaa_extreme")aa=2;
            if(ImGui::Combo("Anti-aliasing",&aa,"Game default\0FXAA\0FXAA strong\0"))rex::cvar::SetFlagByName("swap_post_effect",aa==0?"none":aa==1?"fxaa":"fxaa_extreme");
            int af=number("anisotropic_override",3)+1;
            if(ImGui::Combo("Texture filtering",&af,"Game default\0Off\0 1x\0 2x\0 4x\0 8x\0 16x\0"))rex::cvar::SetFlagByName("anisotropic_override",std::to_string(af-1));
            ImGui::TextWrapped("Anisotropic filtering affects eligible mipmapped textures viewed at an angle. It may have little visible effect on menus and the note highway.");
            bool aspect=read_bool("present_letterbox",true);
            if(ImGui::Checkbox("Preserve aspect ratio",&aspect))rex::cvar::SetFlagByName("present_letterbox",aspect?"true":"false");
            bool fps=show_fps.load();if(ImGui::Checkbox("Show presentation FPS",&fps))rex::cvar::SetFlagByName("gh2_show_fps",fps?"true":"false");
            bool no_audience=gh2::scene::no_audience.load();
            if(ImGui::Checkbox("No audience (hide crowd models)",&no_audience))rex::cvar::SetFlagByName("gh2_no_audience",no_audience?"true":"false");
            ImGui::TextWrapped("Applies immediately. Hides audience geometry while keeping crowd sounds and gameplay intact.");
            bool pending=configured_scale()!=initial_scale_||number("window_width")!=initial_width_||number("window_height")!=initial_height_||number("monitor")!=initial_monitor_||rex::cvar::GetFlagByName("swap_post_effect")!=initial_aa_;
            if(pending)ImGui::TextWrapped("Save and restart normally to apply internal resolution, anti-aliasing, window size or monitor changes.");
            if(ImGui::Button("Save graphics settings")&&rex::cvar::SetFlagByName("gh2_display_fps_limit",std::to_string(fps_limit_edit_))){rex::cvar::SaveConfig(config_);saved_=true;}
            ImGui::SameLine();
            if(ImGui::Button("Restore defaults")) {
                set_scale(1);set_window_size(0);rex::cvar::SetFlagByName("monitor","0");rex::cvar::SetFlagByName("swap_post_effect","none");rex::cvar::SetFlagByName("anisotropic_override","3");rex::cvar::SetFlagByName("present_effect","bilinear");
                rex::cvar::SetFlagByName("fullscreen","true");rex::cvar::SetFlagByName("present_letterbox","true");rex::cvar::SetFlagByName("gh2_display_vsync","false");rex::cvar::SetFlagByName("gh2_show_fps","false");
                rex::cvar::SetFlagByName("gh2_display_fps_limit","0");
                rex::cvar::SetFlagByName("gh2_no_audience","false");
                fps_limit_edit_=0;
            }
            if(saved_)ImGui::TextWrapped(pending?"Saved. Restart normally to apply the pending settings.":"Settings saved.");
        }
        ImGui::End();
    }
public:
    GraphicsDialog(rex::ui::ImGuiDrawer* drawer,std::filesystem::path config):ImGuiDialog(drawer),config_(std::move(config)),
        initial_scale_(gh2::graphics::configured_scale()),initial_width_(gh2::graphics::number("window_width")),initial_height_(gh2::graphics::number("window_height")),initial_monitor_(gh2::graphics::number("monitor")),initial_aa_(rex::cvar::GetFlagByName("swap_post_effect")){}
    void Toggle(){imgui_drawer()->RequestRepaint();visible_=!visible_;if(visible_){RefreshMonitors();fps_limit_edit_=gh2::graphics::number("gh2_display_fps_limit");}}
};
