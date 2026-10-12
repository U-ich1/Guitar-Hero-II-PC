#include "ppc_recomp_shared.h"
#include <rex/rex_app.h>
#include <rex/filesystem.h>
#include <rex/ui/presenter.h>
#include <rex/logging.h>
#include <rex/input/flags.h>
#include <fstream>
#include <thread>
#include "../tests/rex_scripted_input.h"
#include "audio_calibration_dialog.h"
#include <rex/ui/keybinds.h>
#include "user_paths.h"
#include "controls_dialog.h"
#include "graphics_dialog.h"
#include "cheats_dialog.h"
#include "discord_presence.h"
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/input/nop/nop_input_driver.h>

void gh2_rex_register_imports(rex::Runtime&);
void gh2_rex_attach_memory(rex::Runtime&);

static std::function<void()> menu_quit;
void gh2_request_menu_quit() {
    if (menu_quit) menu_quit();
}

class GH2App final : public rex::ReXApp {
    std::jthread capture_thread_;
    gh2::discord::Presence discord_;
    std::filesystem::path capture_directory_;
    std::filesystem::path settings_path_;
    std::unique_ptr<AudioCalibrationDialog> audio_calibration_;
    std::unique_ptr<ControlsDialog> controls_;
    std::unique_ptr<GraphicsDialog> graphics_;
    std::unique_ptr<CheatsDialog> cheats_;
public:
    explicit GH2App(rex::ui::WindowedAppContext& context)
        : ReXApp(context, "Guitar Hero 2", {PPC_CODE_BASE, PPC_CODE_SIZE,
            PPC_IMAGE_BASE, PPC_IMAGE_SIZE, PPCFuncMappings}) {}
    static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& context) {
        return std::make_unique<GH2App>(context);
    }
protected:
    void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
        window()->SetTitle("Guitar Hero 2");
        const auto module=GetModuleHandleW(nullptr);
        const auto icon=FindResourceW(module,MAKEINTRESOURCEW(201),MAKEINTRESOURCEW(10));
        if(icon)window()->SetIcon(LockResource(LoadResource(module,icon)),SizeofResource(module,icon));
        cheats_=std::make_unique<CheatsDialog>(drawer,settings_path_);
        rex::ui::RegisterBind("bind_cheat_settings","F9","Toggle cheats",[this]{cheats_->Toggle();});
        graphics_=std::make_unique<GraphicsDialog>(drawer,settings_path_);
        rex::ui::RegisterBind("bind_graphics_settings","F5","Toggle graphics settings",[this]{graphics_->Toggle();});
        controls_=std::make_unique<ControlsDialog>(drawer,settings_path_);
        rex::ui::RegisterBind("bind_controller_mappings","F6","Toggle controller mappings",[this]{controls_->Toggle();});
        audio_calibration_ = std::make_unique<AudioCalibrationDialog>(drawer, settings_path_);
        if (std::getenv("GH2_OPEN_AUDIO_CALIBRATION")) audio_calibration_->Toggle();
        rex::ui::RegisterBind("bind_audio_calibration", "F8", "Toggle audio calibration", [this] {
            audio_calibration_->Toggle();
        });
    }
    void OnPreSetup(rex::RuntimeConfig& config) override {
        config.gpu_plugin = "xenos";
        REXCVAR_SET(input_backend, std::string("xinput"));
        const char* fixture=std::getenv("GH2_TEST_INPUT");
        const std::string path=fixture?fixture:"";
        config.input_factory = [path](bool tool_mode) {
                auto input=std::make_unique<rex::input::InputSystem>(nullptr);
                if(!tool_mode) {
                    auto direct=std::make_unique<gh2::dinput::Driver>();
                    const bool direct_ready=direct->Setup()==X_STATUS_SUCCESS;
                    auto native=std::make_unique<gh2::dinput::NamedXInput>();
                    if(native->Setup()==X_STATUS_SUCCESS)input->AddDriver(std::move(native));
                    if(direct_ready)input->AddDriver(std::move(direct));
                    auto keyboard=std::make_unique<rex::input::mnk::MnkInputDriver>(nullptr,0);
                    if(keyboard->Setup()==X_STATUS_SUCCESS)input->AddDriver(std::move(keyboard));
                }
                input->AddDriver(std::make_unique<rex::input::nop::NopInputDriver>(nullptr,0));
                input->SetDeviceAssignment(std::make_unique<gh2::controls::ControllerAssignment>());
                if(!path.empty())input->AddDriver(std::make_unique<GH2ScriptedInput>(path.c_str()));
                return input;
            };
    }
    void OnConfigurePaths(rex::PathConfig& paths) override {
        auto root = rex::filesystem::GetExecutableFolder();
        for (unsigned i = 0; i < 4; ++i) root = root.parent_path();
        paths.game_data_root = root / "private";
        const auto user_root = gh2::documents_root();
        gh2::migrate_legacy_data(root / "build/windowed-state/user", root / "build/windowed-state/gh2.toml", user_root);
        paths.user_data_root = user_root / "Saves";
        paths.cache_root = root / "build/windowed-state/cache";
        paths.config_path = user_root / "gh2.toml";
        gh2::repair_legacy_config_path(paths.config_path);
        settings_path_ = paths.config_path;
        capture_directory_ = root / "build/windowed-state";
    }
    void OnPostLoadXexImage() override {
        menu_quit = [this] {
            app_context().CallInUIThreadDeferred([this] {
                REXLOG_INFO("GH2 main-menu Quit selected; requesting normal window close.");
                window()->RequestClose();
            });
        };
        REXLOG_INFO("GH2 save folder: {}", runtime()->user_data_root().string());
        gh2_rex_attach_memory(*runtime());
        gh2_rex_register_imports(*runtime());
    }
    void OnPostSetup() override {
        discord_.Start(capture_directory_.parent_path()/"discord-songs.tsv");
        REXLOG_INFO("GH2 startup graphics: requested scale {}x, AA {}, filtering {}",gh2::graphics::configured_scale(),rex::cvar::GetFlagByName("swap_post_effect"),gh2::graphics::number("anisotropic_override",3));
        auto* input = static_cast<rex::input::InputSystem*>(runtime()->input_system());
        rex::input::X_INPUT_CAPABILITIES capabilities{};
        if (input && input->GetCapabilities(0, 0, &capabilities) == X_ERROR_SUCCESS)
            REXLOG_INFO("GH2 player 1 controller subtype: {}", capabilities.sub_type);
        if (!std::getenv("GH2_CAPTURE_FRAMES") && !std::getenv("GH2_TEST_INPUT")) return;
        std::filesystem::create_directories(capture_directory_);
        capture_thread_ = std::jthread([this](std::stop_token stop) {
            const bool scripted = std::getenv("GH2_TEST_INPUT") != nullptr;
            for (unsigned capture = 1; capture <= (scripted ? 60U : 10U); ++capture) {
                for (unsigned tick = 0; tick < (scripted ? 50U : 150U) && !stop.stop_requested(); ++tick)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (stop.stop_requested()) return;
                rex::ui::RawImage image;
                auto* presenter = runtime()->graphics_system()->presenter();
                if (!presenter || !presenter->CaptureGuestOutput(image)) {
                    REXLOG_WARN("GH2 diagnostic capture {}: guest output unavailable", capture);
                    continue;
                }
                const auto path = capture_directory_ / ("guest-frame-" + std::to_string(capture) + ".ppm");
                std::ofstream file(path, std::ios::binary);
                file << "P6\n" << image.width << ' ' << image.height << "\n255\n";
                for (uint32_t y = 0; y < image.height; ++y)
                    for (uint32_t x = 0; x < image.width; ++x)
                        file.write(reinterpret_cast<const char*>(image.data.data() + y * image.stride + x * 4), 3);
                REXLOG_INFO("GH2 guest frame captured: {} ({}x{})", path.string(), image.width, image.height);
            }
        });
    }
    void OnShutdown() override {
        discord_.Stop();
        rex::ui::UnregisterBind("bind_cheat_settings");cheats_.reset();
        rex::ui::UnregisterBind("bind_graphics_settings");graphics_.reset();
        rex::ui::UnregisterBind("bind_controller_mappings");
        controls_.reset();
        rex::ui::UnregisterBind("bind_audio_calibration");
        audio_calibration_.reset();
        capture_thread_.request_stop();
        if (capture_thread_.joinable()) capture_thread_.join();
    }
};
REX_DEFINE_APP(gh2pc, GH2App::Create)
