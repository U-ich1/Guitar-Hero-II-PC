#pragma once
#include "cheat_settings.h"
#include <rex/ui/imgui_dialog.h>
#include <imgui.h>
#include <filesystem>

class CheatsDialog final : public rex::ui::ImGuiDialog {
    std::filesystem::path config_;
    bool visible_=false,saved_=false;
protected:
    bool WantsContinuousRepaint() const override { return visible_; }
    void OnDraw(ImGuiIO&) override {
        if(!visible_)return;
        ImGui::SetNextWindowSize(ImVec2(560,520),ImGuiCond_FirstUseEver);
        if(ImGui::Begin("Cheats (F9)",&visible_)){
            int choice=gh2::cheats::hyperspeed.load()+1;
            if(ImGui::Combo("Hyperspeed",&choice,"Original cheat codes\0Off\0Level 1 (1.50x)\0Level 2 (1.75x)\0Level 3 (2.00x)\0Level 4 (2.25x)\0Level 5 (2.50x)\0")){
                rex::cvar::SetFlagByName("gh2_hyperspeed",std::to_string(choice-1));saved_=false;
            }
            ImGui::TextWrapped("Choose before loading a song. Changes take effect when the next song or practice session loads; no game restart is needed.");
            ImGui::TextWrapped("Level 1 is GH2's original 1.5x scroll speed. Higher levels progressively speed up scrolling without changing music or note timing.");
            constexpr const char* names[]={"Unlock All", "Air guitar", "Performance mode", "Flaming head", "Crowd heads"};
            for(unsigned i=0;i<gh2::cheats::native.size();++i){auto& setting=gh2::cheats::native[i];int value=setting.mode.load()+1;
                if(ImGui::Combo(names[i],&value,i==4?"Original cheat codes\0Off\0Eyeballs\0Monkeys\0":"Original cheat codes\0Off\0On\0")){rex::cvar::SetFlagByName(setting.key,std::to_string(value-1));saved_=false;}}
            ImGui::TextWrapped("Unlock All refreshes the song list while the main menu is open. If you are elsewhere, return to the main menu to apply it. It disables career and saving while enabled, as in the original game.");
            const int applied=gh2::cheats::last_hyperspeed.load();
            if(applied>=0)ImGui::Text("Last highway loaded: Hyperspeed level %d",applied);
            if(ImGui::Button("Save cheat settings")){rex::cvar::SaveConfig(config_);saved_=true;}
            if(saved_)ImGui::TextUnformatted("Saved for future launches.");
        }
        ImGui::End();
    }
public:
    CheatsDialog(rex::ui::ImGuiDrawer* drawer,std::filesystem::path config):ImGuiDialog(drawer),config_(std::move(config)){}
    void Toggle(){imgui_drawer()->RequestRepaint();visible_=!visible_;}
};
