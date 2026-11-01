#include "audio/Capture.hpp"
#include <SDL.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace brick {
namespace {
class AudioReference {
public:
    AudioReference() {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
            throw std::runtime_error(std::string("Capture audio: ") + SDL_GetError());
    }
    ~AudioReference() { SDL_QuitSubSystem(SDL_INIT_AUDIO); }
};
}

struct Capture::Impl {
    std::unique_ptr<AudioReference> audio;
    SDL_AudioDeviceID device = 0;
    std::vector<std::int16_t> pcm;
    std::atomic<std::uint32_t> written{0}, clipped{0};
    std::atomic<int> peak{0};
    std::atomic<bool> active{false}, limitReached{false};
    bool started = false;

    ~Impl() { close(); }
    void close() {
        active.store(false, std::memory_order_release);
        if (device) { SDL_CloseAudioDevice(device); device = 0; }
        audio.reset();
    }
    static void callback(void* userdata, Uint8* stream, int length) noexcept {
        auto& self = *static_cast<Impl*>(userdata);
        if (!self.active.load(std::memory_order_acquire) || length <= 0) return;
        const auto offset = self.written.load(std::memory_order_relaxed);
        const auto count = std::min<std::size_t>(std::size_t(length) / sizeof(std::int16_t),
                                               self.pcm.size() - offset);
        std::memcpy(self.pcm.data() + offset, stream, count * sizeof(std::int16_t));
        int peakValue = 0;
        std::uint32_t clipping = 0;
        for (std::size_t n = offset; n < offset + count; ++n) {
            const int value = self.pcm[n];
            peakValue = std::max(peakValue, std::abs(value));
            if (value == -32768 || value == 32767) ++clipping;
        }
        self.peak.store(peakValue, std::memory_order_relaxed);
        self.clipped.fetch_add(clipping, std::memory_order_relaxed);
        self.written.store(offset + static_cast<std::uint32_t>(count), std::memory_order_release);
        if (offset + count == self.pcm.size()) {
            self.limitReached.store(true, std::memory_order_release);
            self.active.store(false, std::memory_order_release);
        }
    }
};

Capture::Capture() : impl_(std::make_unique<Impl>()) {}
Capture::~Capture() = default;

std::vector<CaptureDevice> Capture::devices() {
    AudioReference reference;
    std::vector<CaptureDevice> result{{-1, "SYSTEM DEFAULT"}};
    const int count = SDL_GetNumAudioDevices(1);
    if (count < 0) throw std::runtime_error(std::string("Capture devices: ") + SDL_GetError());
    for (int index = 0; index < count; ++index) {
        if (const char* name = SDL_GetAudioDeviceName(index, 1)) result.push_back({index, name});
    }
    return result;
}

void Capture::start(int deviceIndex, double maxSeconds) {
    if (!std::isfinite(maxSeconds) || maxSeconds < 1 || maxSeconds > 300)
        throw std::invalid_argument("Recording length must be 1..300 seconds");
    if (impl_->started) throw std::logic_error("Finish or cancel the current recording first");
    auto audio = std::make_unique<AudioReference>();
    std::string deviceName;
    if (deviceIndex < -1) throw std::out_of_range("Capture device index");
    if (deviceIndex >= 0) {
        const char* name = SDL_GetAudioDeviceName(deviceIndex, 1);
        if (!name) throw std::runtime_error(std::string("Capture device unavailable: ") + SDL_GetError());
        deviceName = name;
    }
    impl_->pcm.assign(static_cast<std::size_t>(maxSeconds * sampleRate), 0);
    impl_->written.store(0); impl_->clipped.store(0); impl_->peak.store(0);
    impl_->limitReached.store(false);
    SDL_AudioSpec wanted{}, obtained{};
    wanted.freq = sampleRate; wanted.format = AUDIO_S16SYS;
    wanted.channels = 1; wanted.samples = 1024;
    wanted.callback = &Impl::callback; wanted.userdata = impl_.get();
    impl_->device = SDL_OpenAudioDevice(deviceIndex < 0 ? nullptr : deviceName.c_str(),
                                       1, &wanted, &obtained, 0);
    if (!impl_->device) {
        impl_->pcm.clear();
        throw std::runtime_error(std::string("Microphone unavailable: ") + SDL_GetError());
    }
    impl_->audio = std::move(audio);
    impl_->started = true;
    impl_->active.store(true, std::memory_order_release);
    SDL_PauseAudioDevice(impl_->device, 0);
}

CaptureStatus Capture::status() {
    if (impl_->device && (!impl_->active.load(std::memory_order_acquire) ||
                         SDL_GetAudioDeviceStatus(impl_->device) == SDL_AUDIO_STOPPED))
        impl_->close();
    CaptureStatus result;
    result.recording = impl_->active.load(std::memory_order_acquire);
    result.limitReached = impl_->limitReached.load(std::memory_order_acquire);
    result.frames = impl_->written.load(std::memory_order_acquire);
    result.clippedFrames = impl_->clipped.load(std::memory_order_relaxed);
    result.seconds = double(result.frames) / sampleRate;
    result.peak = float(impl_->peak.load(std::memory_order_relaxed)) / 32768.0f;
    return result;
}

Sample Capture::finish() {
    if (!impl_->started) throw std::logic_error("No recording to keep");
    impl_->close();
    impl_->started = false;
    impl_->pcm.resize(impl_->written.load(std::memory_order_acquire));
    Sample result;
    result.name = "MIC RECORDING";
    result.pcm = std::move(impl_->pcm);
    const int tuning = int(std::lround(1536.0 * std::log2(double(sampleRate) / 8363.0)));
    const int relative = int(std::floor((tuning + 64) / 128.0));
    result.relativeNote = static_cast<std::int8_t>(relative);
    result.finetune = static_cast<std::int8_t>(tuning - relative * 128);
    return result;
}

void Capture::cancel() {
    impl_->close();
    impl_->started = false;
    std::vector<std::int16_t>().swap(impl_->pcm);
    impl_->written.store(0); impl_->clipped.store(0); impl_->peak.store(0);
    impl_->limitReached.store(false);
}

} // namespace brick
