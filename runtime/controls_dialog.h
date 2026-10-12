#pragma once
#include "controller_mapping.h"
#include "controller_assignment.h"
#include "directinput_driver.h"
#include <rex/ui/imgui_dialog.h>
#include <rex/cvar.h>
#include <imgui.h>
#include <filesystem>
#include <vector>
#include <charconv>

namespace gh2::controls {
inline std::vector<rex::cvar::FlagRegistrar> register_flags() {
    std::vector<rex::cvar::FlagRegistrar> flags;
    auto add=[&](std::string name,std::atomic<int>& setting,int maximum,const char* description) {
        auto* value=&setting;
        rex::cvar::FlagEntry entry{name,rex::cvar::FlagType::Int32,"Input/Controller mappings",description,
            [value,maximum](std::string_view text) {
                int result; auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),result);
                if(error!=std::errc{}||end!=text.data()+text.size()||result<0||result>maximum)return false;
                value->store(result);return true;
            }, [value]{return std::to_string(value->load());}, {}, rex::cvar::Lifecycle::kHotReload, {}, std::to_string(setting.load())};
        flags.emplace_back(std::move(entry));
    };
    for(bool guitar:{false,true}) {
        auto& settings=guitar?guitar_settings:gamepad_settings;
        const std::string prefix=guitar?"guitar_":"gamepad_";
        add(prefix+"mode",settings.mode,2,"0: native; 1: remapped; 2: strummed guitar layout");
        add(prefix+"whammy",settings.whammy,4,"0: native; 1: LX; 2: LY; 3: RX; 4: RY");
        for(int i=0;i<ActionCount;++i)add(prefix+"bind_"+std::to_string(i),settings.sources[i],SourceCount-1,action_names[i]);
    }
    add("player_one_device",player_one_device,4,"0: automatic; 1-4: XInput slot for player one");
    add("gh2_controller_api",controller_api,2,"Controller API: 0 automatic, 1 XInput, 2 DirectInput");
    flags.emplace_back(rex::cvar::FlagEntry{"player_one_device_key",rex::cvar::FlagType::String,"Input/Controller mappings","Persistent controller identity for player one",
        [](std::string_view value){std::lock_guard lock(catalog_mutex);selected_device_key=std::string(value);return true;},
        []{return selected_key();},{},rex::cvar::Lifecycle::kHotReload,{},""});
    for(unsigned i=0;i<16;++i)add("dinput_button_"+std::to_string(i),gh2::dinput::button_sources[i],128,"DirectInput source button: 0 unbound, 1-128 button number");
    for(unsigned i=0;i<4;++i)add("dinput_axis_"+std::to_string(i),gh2::dinput::axis_sources[i],7,"DirectInput axis: X, Y, Z, RX, RY, RZ, slider 1, slider 2");
    for(unsigned i=0;i<2;++i){add("dinput_trigger_axis_"+std::to_string(i),gh2::dinput::trigger_axes[i],8,"0: digital button only; 1-8: analogue trigger axis");
        add("dinput_trigger_mode_"+std::to_string(i),gh2::dinput::trigger_modes[i],3,"Trigger rest/direction: minimum, maximum, center positive, center negative");}
    return flags;
}
inline auto registered_flags=register_flags();
}
class ControlsDialog final : public rex::ui::ImGuiDialog {
    std::filesystem::path config_;
    bool visible_=false;
    int profile_=0,player_=0;
protected:
    bool WantsContinuousRepaint() const override { return visible_; }
    void OnDraw(ImGuiIO&) override {
        using namespace gh2::controls;
        if(!visible_)return;
        ImGui::SetNextWindowSize(ImVec2(560,620),ImGuiCond_FirstUseEver);
        if(ImGui::Begin("Controller mappings (F6)",&visible_)) {
            ImGui::Combo("Profile",&profile_,"Gamepad\0Guitar\0");
            const auto devices=catalog();const auto key=selected_key();
            std::string selected=key.empty()?(player_one_device.load()?"XInput slot "+std::to_string(player_one_device.load()):"Automatic"):"Selected controller disconnected";
            for(const auto& device:devices)if((!key.empty()&&device.guid==key)||(key.empty()&&uint64_t(device.id)==(0x58490000ULL|uint64_t(std::max(player_one_device.load(),1)-1)))) {
                if(!key.empty()||player_one_device.load())selected=gh2::dinput::device_label(device);
            }
            if(ImGui::BeginCombo("Player 1 device",selected.c_str())) {
                if(ImGui::Selectable("Automatic",key.empty()&&player_one_device.load()==0)) {
                    rex::cvar::SetFlagByName("player_one_device_key","");rex::cvar::SetFlagByName("player_one_device","0");
                }
                for(const auto& device:devices) {
                    ImGui::PushID(device.guid.c_str());
                    const auto label=gh2::dinput::device_label(device);
                    if(ImGui::Selectable(label.c_str(),device.guid==key))rex::cvar::SetFlagByName("player_one_device_key",device.guid);
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            ImGui::Combo("Monitor player",&player_,"1\0 2\0 3\0 4\0");
            auto& settings=profile_?guitar_settings:gamepad_settings;
            const std::string prefix=profile_?"guitar_":"gamepad_";
            auto profile=settings.snapshot();
            if(ImGui::Combo("Controls",&profile.mode,"Native GH2 controls\0Custom bindings\0Custom guitar with strum\0"))
                rex::cvar::SetFlagByName(prefix+"mode",std::to_string(profile.mode));
            ImGui::TextWrapped("Native preserves your working controls. Custom gamepad uses GH2's automatic strumming. Guitar mode requires a separate strum.");
            ImGui::BeginDisabled(profile.mode==0);
            for(int action=0;action<ActionCount;++action) {
                int source=profile.sources[action];
                if(ImGui::BeginCombo(action_names[action],source_names[source])) {
                    for(int choice=0;choice<SourceCount;++choice)
                        if(ImGui::Selectable(source_names[choice],choice==source))
                            rex::cvar::SetFlagByName(prefix+"bind_"+std::to_string(action),std::to_string(choice));
                    ImGui::EndCombo();
                }
            }
            if(ImGui::Combo("Whammy axis",&profile.whammy,"Native axes\0Left stick X\0Left stick Y\0Right stick X\0Right stick Y\0"))
                rex::cvar::SetFlagByName(prefix+"whammy",std::to_string(profile.whammy));
            ImGui::EndDisabled();
            if(ImGui::Button("Restore native profile")) {
                const auto original=defaults(profile_!=0);
                rex::cvar::SetFlagByName(prefix+"mode","0");rex::cvar::SetFlagByName(prefix+"whammy","0");
                for(int i=0;i<ActionCount;++i)rex::cvar::SetFlagByName(prefix+"bind_"+std::to_string(i),std::to_string(original.sources[i]));
            }
            ImGui::SameLine();
            if(ImGui::Button("Save"))rex::cvar::SaveConfig(config_);
            ImGui::Separator();
            ImGui::Text("Player %d: subtype %u",player_+1,unsigned(detected_type[player_].load()));
            State live{live_buttons[player_].load(),live_lt[player_].load(),live_rt[player_].load()};
            for(int source=1;source<SourceCount;++source)if(pressed(live,source))ImGui::Text("Pressed: %s",source_names[source]);
            ImGui::Text("LX %d  LY %d  RX %d  RY %d",int(live_axes[player_][0].load()),int(live_axes[player_][1].load()),int(live_axes[player_][2].load()),int(live_axes[player_][3].load()));
            if(ImGui::CollapsingHeader("DirectInput device setup")) {
                {std::lock_guard lock(gh2::dinput::live_mutex);ImGui::TextWrapped("Last polled: %s",gh2::dinput::live_name.empty()?"No DirectInput controller input yet":gh2::dinput::live_name.c_str());}
                ImGui::TextWrapped("DirectInput buttons differ between models. Map their raw button numbers into standard gamepad inputs here, then use the fret profile above.");
                const char* targets[]={"A","B","X","Y","LB","RB","Back","Start","Left stick click","Right stick click","LT","RT","D-pad up","D-pad down","D-pad left","D-pad right"};
                for(unsigned i=0;i<16;++i){
                    int value=gh2::dinput::button_sources[i].load();ImGui::PushID(int(i));
                    if(ImGui::InputInt(targets[i],&value))rex::cvar::SetFlagByName("dinput_button_"+std::to_string(i),std::to_string(std::clamp(value,0,128)));
                    ImGui::PopID();
                }
                const char* axes[]={"LX axis","LY axis","RX axis","RY axis"};
                for(unsigned i=0;i<4;++i){int value=gh2::dinput::axis_sources[i].load();
                    if(ImGui::Combo(axes[i],&value,"X\0Y\0Z\0RX\0RY\0RZ\0Slider 1\0Slider 2\0"))rex::cvar::SetFlagByName("dinput_axis_"+std::to_string(i),std::to_string(value));
                }
                for(unsigned i=0;i<2;++i){ImGui::PushID(int(100+i));int axis=gh2::dinput::trigger_axes[i].load(),mode=gh2::dinput::trigger_modes[i].load();
                    ImGui::TextUnformatted(i==0?"Left trigger":"Right trigger");
                    if(ImGui::Combo("Trigger axis",&axis,"Button only\0X\0Y\0Z\0RX\0RY\0RZ\0Slider 1\0Slider 2\0"))rex::cvar::SetFlagByName("dinput_trigger_axis_"+std::to_string(i),std::to_string(axis));
                    if(ImGui::Combo("Rest / direction",&mode,"Rest at minimum\0Rest at maximum\0Centered, positive side\0Centered, negative side\0"))rex::cvar::SetFlagByName("dinput_trigger_mode_"+std::to_string(i),std::to_string(mode));ImGui::PopID();
                }
                for(unsigned i=0;i<128;++i)if(gh2::dinput::live_buttons[i].load())ImGui::Text("Raw button %u pressed",i+1);
                for(unsigned i=0;i<8;++i)ImGui::Text("Raw axis %u: %d",i+1,int(gh2::dinput::live_axes[i].load()));
            }
            ImGui::TextWrapped("Pause before editing. Custom bindings also affect menu buttons; green confirms and red goes back in guitar mode. Physical tilt axes are preserved.");
        }
        ImGui::End();
    }
public:
    ControlsDialog(rex::ui::ImGuiDrawer* drawer,std::filesystem::path config):ImGuiDialog(drawer),config_(std::move(config)){}
    void Toggle(){imgui_drawer()->RequestRepaint();visible_=!visible_;}
};
