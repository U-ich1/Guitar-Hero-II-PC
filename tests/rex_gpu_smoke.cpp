// Exercises the real Xenos/D3D12 backend without executing GH2 or claiming a frame.
#include <rex/runtime.h>
#include <rex/system/gpu_plugin.h>
#include <rex/logging.h>
#include <iostream>
int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: gh2_rex_gpu_smoke <game-data-directory> <scratch-directory>\n";
        return 1;
    }
    try {
        const std::filesystem::path scratch(argv[2]);
        std::filesystem::create_directories(scratch);
        rex::InitLogging((scratch / "gpu.log").string().c_str(), spdlog::level::debug);
        struct LoggingGuard { ~LoggingGuard() { rex::ShutdownLogging(); } } logging_guard;
        rex::RuntimeConfig config;
        config.graphics = rex::system::LoadGpuPlugin("xenos", "d3d12");
        if (!config.graphics) { std::cerr << "GPU plugin load failed\n"; return 2; }
        rex::Runtime runtime(argv[1], scratch / "user", {}, scratch / "cache");
        const auto result = runtime.Setup(std::move(config));
        if (result != 0) {
            std::cerr << "Xenos GPU setup failed: 0x" << std::hex << result << '\n';
            return 3;
        }
        std::cout << "Xenos GPU runtime initialized; no guest game code or frame executed.\n";
        runtime.Shutdown();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 4;
    }
}
