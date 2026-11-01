#include "ui/TrackerApp.hpp"
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    brick::AppOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto pathArgument = [&]() -> const char* {
            if (++i >= argc) throw std::runtime_error("Missing path after " + arg);
            return argv[i];
        };
        try {
            if (arg == "--open") options.openPath = pathArgument();
            else if (arg == "--screenshot") options.screenshotPath = pathArgument();
            else if (arg == "--data-dir") options.dataDirectory = pathArgument();
            else if (arg == "--smoke") options.smoke = true;
            else if (arg == "--no-audio") options.noAudio = true;
            else if (arg == "--fullscreen") options.fullscreen = true;
            else if (arg == "--ui-test") options.uiTest = true;
            else if (arg == "--demo") options.demo = true;
            else if (arg == "--help" || arg == "-h") {
                std::cout << "Little XM Tracker\n  --open song.xm  --demo  --data-dir path\n"
                             "  --screenshot image.bmp  --smoke  --ui-test  --no-audio  --fullscreen\n"
                             "Gamepad: D-pad, A, B, Start, Select.\n";
                return 0;
            } else throw std::runtime_error("Unknown argument: " + arg);
        } catch (const std::exception& e) {
            std::cerr << e.what() << '\n';
            return 2;
        }
    }
    if(options.uiTest) {
        if(options.dataDirectory.empty()) { std::cerr << "--ui-test requires an explicit --data-dir\n"; return 2; }
        options.noAudio = true;
    }
    try {
        brick::TrackerApp app(std::move(options));
        return app.run();
    } catch (const std::exception& e) {
        std::cerr << "Little XM Tracker: " << e.what() << '\n';
        return 1;
    }
}
