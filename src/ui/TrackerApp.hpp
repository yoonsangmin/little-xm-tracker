#pragma once
#include <filesystem>
#include <memory>

namespace brick {
struct AppOptions {
    std::filesystem::path openPath;
    std::filesystem::path screenshotPath;
    std::filesystem::path dataDirectory;
    bool smoke = false;
    bool noAudio = false;
    bool fullscreen = false;
    bool uiTest = false;
    bool demo = false;
};

class TrackerApp {
public:
    explicit TrackerApp(AppOptions options);
    ~TrackerApp();
    int run();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace brick
