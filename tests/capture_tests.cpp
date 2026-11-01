#include "audio/Capture.hpp"
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    // This test must never open a real microphone, even when run manually.
    SDL_SetMainReady();
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    try {
        require(SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER) == 0, SDL_GetError());
        require(std::string(SDL_GetCurrentAudioDriver()) == "dummy", "Dummy capture driver is required");
        brick::Capture capture;
        require(!capture.status().recording, "Constructing capture must not record");
        require(!brick::Capture::devices().empty(), "System default input must be listed");
        bool rejected = false;
        try { capture.start(-1, 0); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected && !capture.status().recording, "Invalid limits must not start capture");
        capture.start(-1, 1);
        const Uint32 deadline = SDL_GetTicks() + 4000;
        while (capture.status().recording && Sint32(deadline - SDL_GetTicks()) > 0) SDL_Delay(10);
        const auto complete = capture.status();
        require(complete.limitReached && !complete.recording, "Capture must stop at its frame limit");
        require(complete.frames == brick::Capture::sampleRate, "Capture limit must be frame exact");
        require(complete.clippedFrames == 0 && complete.peak == 0, "Dummy input is silent");
        const auto sample = capture.finish();
        require(sample.pcm.size() == brick::Capture::sampleRate && sample.sixteenBit, "Finished capture returns mono16 PCM");
        require(std::all_of(sample.pcm.begin(), sample.pcm.end(), [](auto value) { return value == 0; }), "Dummy PCM must remain silent");
        const double frequency = 8363.0 * std::exp2((sample.relativeNote + sample.finetune / 128.0) / 12.0);
        require(std::abs(frequency - brick::Capture::sampleRate) < 25, "Recording must preserve its XM root pitch");
        capture.start(-1, 1);
        capture.cancel();
        require(!capture.status().recording && capture.status().frames == 0, "Cancel must close and discard input");
        SDL_Quit();
        std::cout << "CAPTURE PASS: bounded dummy input, sample pitch and cancellation\n";
    } catch (const std::exception& e) {
        SDL_Quit();
        std::cerr << e.what() << '\n';
        return 1;
    }
}
