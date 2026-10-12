#include "../runtime/user_paths.h"
#include <iostream>
#include <iterator>
static std::string read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), {});
}
int main(int argc, char** argv) {
    try {
        if (argc != 2) return 1;
        const std::filesystem::path root(argv[1]), legacy = root / "legacy", target = root / "destination";
        std::filesystem::create_directories(legacy / "profile/title");
        std::filesystem::create_directories(target / "Saves/profile/title");
        std::ofstream(legacy / "profile/title/save.dat") << "old save";
        std::ofstream(legacy / "profile/title/header") << "old header";
        std::ofstream(legacy / "config.toml") << "audio_offset_ms = 5\n";
        std::ofstream(target / "Saves/profile/title/save.dat") << "new save";
        const auto copied = gh2::migrate_legacy_data(legacy, legacy / "config.toml", target);
        if (copied != 3 || read(target / "Saves/profile/title/save.dat") != "new save" ||
            read(legacy / "profile/title/save.dat") != "old save" ||
            read(target / "gh2.toml") != read(legacy / "config.toml"))
            throw std::runtime_error("Migration overwrite/preservation failed");
        std::ofstream(legacy / "profile/title/header") << "changed old header";
        if (gh2::migrate_legacy_data(legacy, legacy / "config.toml", target) != 0 ||
            read(target / "Saves/profile/title/header") != "old header")
            throw std::runtime_error("Repeat launch re-imported stale data");
        std::cout << "PASS: migration preserves originals and newer destinations; settings copied; repeat import prevented.\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
}
