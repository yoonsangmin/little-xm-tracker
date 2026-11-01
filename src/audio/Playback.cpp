#include "audio/Playback.hpp"
#include "io/Codec.hpp"
#include "model/Document.hpp"

#include <SDL.h>
#include <PlayerSTD.h>
#include <XModule.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

namespace brick {
namespace {

// MilkyPlay mix() processes its configured buffer size, not its argument.
// Use exactly one native mixer packet: 192 frames at 48 kHz, 176 at 44.1 kHz.
// Storage stays fixed, and already-rendered edit latency stays at most 4 ms.
constexpr std::size_t maxBlockFrames = Playback::sampleRate / 250;

// MilkyTracker's default playback and WAV output use master volume 256 and
// MasterMixer sampleShift=1. ChannelMixer alone produces twice that level.
// Retain the upstream clamp-before-shift order, with portable arithmetic
// division for negative odd samples (signed right shift is not portable in C++17).
constexpr mp_sint32 outputMixMinimum = -65536, outputMixMaximum = 65535;
std::int16_t outputSample(mp_sint32 mixed) noexcept {
    const auto bounded = std::clamp(mixed, outputMixMinimum, outputMixMaximum);
    return static_cast<std::int16_t>(bounded / 2 - (bounded < 0 && bounded % 2 != 0 ? 1 : 0));
}

MixerSettings::ResamplerTypes interpolationResampler(Interpolation interpolation) {
    switch (interpolation) {
    case Interpolation::Nearest: return MixerSettings::MIXER_NORMAL_RAMPING;
    case Interpolation::Linear: return MixerSettings::MIXER_LERPING_RAMPING;
    case Interpolation::CubicLagrange: return MixerSettings::MIXER_LAGRANGE_RAMPING;
    case Interpolation::CubicSpline: return MixerSettings::MIXER_SPLINE_RAMPING;
    case Interpolation::Sinc: return MixerSettings::MIXER_SINCTABLE_RAMPING;
    case Interpolation::SincPrecise: return MixerSettings::MIXER_SINC_RAMPING;
    }
    throw std::invalid_argument("Unsupported audio interpolation");
}

void validateAudioSettings(const AudioSettings& settings) {
    if (settings.sampleRate != 44100 && settings.sampleRate != 48000)
        throw std::invalid_argument("Audio sample rate must be 44100 or 48000 Hz");
    (void)interpolationResampler(settings.interpolation);
}

class MemoryFile final : public XMFileBase {
public:
    explicit MemoryFile(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}
    mp_sint32 read(void* destination, mp_sint32 elementSize, mp_sint32 count) override {
        if (elementSize <= 0 || count <= 0) return 0;
        const auto requested = static_cast<std::size_t>(elementSize) * static_cast<std::size_t>(count);
        const auto available = bytes_.size() - position_;
        const auto amount = std::min(requested, available);
        if (amount) std::memcpy(destination, bytes_.data() + position_, amount);
        position_ += amount;
        return static_cast<mp_sint32>(amount);
    }
    mp_sint32 write(const void*, mp_sint32, mp_sint32) override { return 0; }
    void seek(mp_uint32 offset, SeekOffsetTypes origin = SeekOffsetTypeStart) override {
        std::int64_t target = offset;
        if (origin == SeekOffsetTypeCurrent)
            target = static_cast<std::int64_t>(position_) + static_cast<std::int32_t>(offset);
        else if (origin == SeekOffsetTypeEnd)
            target = static_cast<std::int64_t>(bytes_.size()) + static_cast<std::int32_t>(offset);
        if (target < 0 || static_cast<std::uint64_t>(target) > bytes_.size())
            throw std::runtime_error("MilkyPlay attempted to seek outside the XM buffer");
        position_ = static_cast<std::size_t>(target);
    }
    mp_uint32 pos() override { return static_cast<mp_uint32>(position_); }
    mp_uint32 size() override { return static_cast<mp_uint32>(bytes_.size()); }
    const SYSCHAR* getFileName() override { return nullptr; }
    const char* getFileNameASCII() override { return "memory.xm"; }
    bool isOpen() override { return true; }
    bool isOpenForWriting() override { return false; }
private:
    const std::vector<std::uint8_t>& bytes_;
    std::size_t position_ = 0;
};

class CorePlayer final : public PlayerSTD {
public:
    explicit CorePlayer(const AudioSettings& settings) : PlayerSTD(settings.sampleRate) {
        setBufferSize(getBeatPacketSize());
        setPlayMode(PlayMode_FastTracker2);
        setResamplerType(interpolationResampler(settings.interpolation));
        setRamp(true);
        // Upstream's reset-on-halt allocates a temporary channel array. The
        // adapter fades halted voices using stopSample() instead, in the callback.
        resetOnStop(false);
    }
    // Read protected transport state, without changing upstream or effect logic.
    // adder becomes zero when MilkyPlay finishes its one-row audition mode.
    PlaybackStatus transport() const {
        PlaybackStatus result;
        result.playing = startPlay && !idle && !halted && adder != 0 && !renderRangeEnded_;
        result.rowAudition = result.playing && playOneRowOnly;
        result.order = poscnt;
        result.row = rowcnt;
        result.pattern = patternIndexToPlay >= 0 ? patternIndexToPlay :
            (module && poscnt >= 0 && poscnt < module->header.ordnum ? module->header.ord[poscnt] : 0);
        result.bpm = bpm;
        result.speed = tickSpeed;
        result.loopFirstRow = loopFirstRow_;
        result.loopEndRow = loopEndRow_;
        return result;
    }
    bool isRowAudition() const { return playOneRowOnly && !idle; }
    bool hasFinishedAudition() const { return isRowAudition() && adder == 0; }
    bool isHeldRowPreview() const { return heldRowPreview_; }
    bool needsPreviewPattern() const { return heldRowPreview_ && !idle; }
    bool isReadingPreviewRow() const { return needsPreviewPattern() && !heldRowEnded_; }
    void beginHeldRowPreview() {
        heldRowPreview_ = true;
        heldRowEnded_ = false;
        BPMCounter = 0;
        setIdle(false);
    }
    void stopAudition() {
        if (heldRowPreview_) {
            // A row may have changed global volume, tempo or effect flags.
            // Reset its sequencer as well as voices before isolated note use.
            setPatternToPlay(-1);
            restart();
            setIdle(true);
        }
        resetChannelsFull();
    }
    void restart(mp_uint32 startPosition = 0, mp_uint32 startRow = 0,
                 bool resetMixer = true, const mp_ubyte* customPanningTable = nullptr,
                 bool oneRowOnly = false) override {
        clearRowLoop();
        clearRenderOrderRange();
        heldRowPreview_ = false;
        heldRowEnded_ = false;
        if (resetMixer) clearMonitorNotes();
        PlayerSTD::restart(startPosition, startRow, resetMixer, customPanningTable, oneRowOnly);
    }
    void clearMonitorNotes() { monitorNotes_.fill(0); }
    int monitorNote(int channelIndex) const { return monitorNotes_[static_cast<std::size_t>(channelIndex)]; }
    void setRowLoop(int first, int end) { loopFirstRow_ = first; loopEndRow_ = end; }
    void clearRowLoop() { loopFirstRow_ = loopEndRow_ = -1; }
    void setRenderOrderRange(int first, int last) {
        renderFirstOrder_ = first;
        renderLastOrder_ = last;
        renderRangeEnded_ = false;
        // Each offline pass starts with the same fresh mixer-tick phase.
        BPMCounter = 0;
    }
    bool renderOrderRangeEnded() const { return renderRangeEnded_; }
    void timerHandler(mp_sint32 packet) override {
        // MilkyPlay's raw BPM adder assumes 250 packets/second. Its packet
        // length is truncated at 44.1 kHz (176 frames), so scale only the
        // current packet's timer increment by its actual elapsed time.
        // Keep the raw value outside this call: Fxx/restart write raw adders.
        const auto rawAdder = adder;
        const auto oldBpm = bpm;
        const auto oldBaseBpm = baseBpm;
        const bool correctTiming = getMixFrequency() != Playback::sampleRate;
        const auto packetAdder = correctTiming ? static_cast<mp_uint32>(
            static_cast<std::uint64_t>(rawAdder) * getBeatPacketSize() * MP_TIMERFREQ / getMixFrequency()) : rawAdder;
        const auto nextCounter = static_cast<std::uint64_t>(BPMCounter) + packetAdder;
        const bool tickDue = nextCounter >> 32;
        if (renderLastOrder_ >= 0 && !idle && ticker == 0 && tickDue &&
            (poscnt < renderFirstOrder_ || poscnt > renderLastOrder_)) {
            // PlayerSTD advances the cursor during the last tick of the old
            // row. Wait for the next row's first tick so all old-row PCM is
            // retained, then stop before any outside note/effect is processed.
            renderRangeEnded_ = true;
            setDisableMixing(true);
            return;
        }
        if (rawAdder) lastRunningAdder_ = rawAdder;
        const auto previousSync = synccnt;
        const bool finishHeldRow = heldRowPreview_ && heldRowEnded_ && tickDue && !idle && !halted;
        if (correctTiming) adder = packetAdder;
        PlayerSTD::timerHandler(packet);
        // Every upstream tempo/base-tempo write also calculates a new raw
        // adder. Otherwise restore ours, including several Fxx commands that
        // finish at the original BPM. Zero is a halt/one-row stop, never undo it.
        if (correctTiming && adder && bpm == oldBpm && baseBpm == oldBaseBpm) adder = rawAdder;
        // The scratch pattern supplies a blank row after the selected one.
        // Its first tick naturally clears upstream's row-effect overrides
        // (DFS/DVS), without retriggering notes or changing instrument state.
        // Then idle keeps only the instrument clock running. In particular we
        // never use oneRowOnly, whose early return freezes the entire clock.
        if (heldRowPreview_ && halted && !idle) {
            // F00 still means stop. Do not leave a halted loop voice sounding
            // forever with a frozen instrument clock; no callback allocation.
            resetChannelsFull();
            setIdle(true);
        } else if (finishHeldRow) setIdle(true);
        else if (heldRowPreview_ && !idle && !halted && synccnt != previousSync)
            heldRowEnded_ = true;
        if (loopFirstRow_ >= 0 && !idle && !halted && synccnt != previousSync &&
            (rowcnt < loopFirstRow_ || rowcnt >= loopEndRow_)) {
            // Redirect only after MilkyPlay completes the row (including delay
            // ticks). Keep mixer voices, tempo, envelopes and effect memory;
            // no restart, allocation or UI-thread timing at loop boundaries.
            setPatternPos(0, static_cast<mp_uint32>(loopFirstRow_), false, false);
        }
        if (tickDue && !idle && !halted) {
            // grabChannelInfo().note is the current row's command, so an empty
            // row reports zero even while the previous note is still sounding.
            // Capture note events at the engine tick (including delayed notes)
            // instead of sampling them at the slower UI refresh rate. This
            // fixed cache never allocates and never changes player state.
            const int count=std::min<int>(initialNumChannels, static_cast<int>(monitorNotes_.size()));
            for (int channelIndex=0;channelIndex<count;++channelIndex) {
                TPlayerChannelInfo info{};
                grabChannelInfo(channelIndex,info);
                if (info.note>=1 && info.note<=96 && isChannelPlaying(channelIndex))
                    monitorNotes_[static_cast<std::size_t>(channelIndex)]=info.note;
            }
        }
    }
    void releaseForTail() {
        clearRenderOrderRange();
        for (int channelIndex = 0; channelIndex < initialNumChannels; ++channelIndex) {
            TPlayerChannelInfo info{};
            grabChannelInfo(channelIndex, info);
            if (info.instrument) playNote(static_cast<mp_ubyte>(channelIndex), XModule::NOTE_OFF, info.instrument);
        }
        idle = true;
        halted = false;
        adder = lastRunningAdder_;
    }
private:
    void clearRenderOrderRange() {
        if (renderRangeEnded_) setDisableMixing(false);
        renderFirstOrder_ = renderLastOrder_ = -1;
        renderRangeEnded_ = false;
    }
    int renderFirstOrder_ = -1, renderLastOrder_ = -1;
    bool renderRangeEnded_ = false;
    bool heldRowPreview_ = false;
    bool heldRowEnded_ = false;
    int loopFirstRow_ = -1, loopEndRow_ = -1;
    mp_uint32 lastRunningAdder_ = 0;
    std::array<int,32> monitorNotes_{};
};

// Raw PCM audition uses the upstream mixer, independently of instrument mapping.
class SampleMixer final : public ChannelMixer {
public:
    explicit SampleMixer(const AudioSettings& settings) : ChannelMixer(1, settings.sampleRate) {
        setBufferSize(getBeatPacketSize());
        setResamplerType(interpolationResampler(settings.interpolation));
        setRamp(true);
        initDevice();
        startPlay = true;
        startMixer();
    }
    void timerHandler(mp_sint32) override {}
};

struct Core {
    XModule module;
    CorePlayer song;
    CorePlayer audition;
    SampleMixer sample;
    std::vector<std::size_t> sampleCounts;
    Document document;
    std::vector<mp_ubyte*> retiredSamples;
    std::size_t retiredBytes = 0;
    bool endedVoicesStopped = false;
    int previewPattern = 0, previewRow = 0, previewOverrideChannel = -1;
    std::array<mp_ubyte, 6> previewOverride{};
    std::array<mp_ubyte, 2 * 32 * 6> previewPatternData{};

    Core(const Document& source, const AudioSettings& settings)
        : song(settings), audition(settings), sample(settings), document(source) {
        retiredSamples.reserve(1024);
        const auto bytes = io::encodeXm(document);
        MemoryFile file(bytes);
        if (module.loadModule(file) != MP_OK)
            throw std::runtime_error("MilkyPlay could not load the XM playback snapshot");
        for (const auto& instrument : document.instruments)
            sampleCounts.push_back(instrument.samples.size());
        if (song.startPlaying(&module, false, 0, 0, -1, nullptr, true) != MP_OK ||
            audition.startPlaying(&module, true, 0, 0, -1, nullptr, true) != MP_OK)
            throw std::runtime_error("MilkyPlay could not initialize playback");
    }

    void checkPattern(int pattern, int row) const {
        if (pattern < 0 || pattern >= 256 || row < 0 ||
            row >= (module.phead[pattern].patternData ? module.phead[pattern].rows : 64))
            throw std::out_of_range("Playback pattern or row");
    }
    void preparePattern(int pattern) {
        auto& destination = module.phead[pattern];
        if (destination.patternData) return;
        // XModule owns 256 headers, but LoaderXM only fills through the last
        // stored/order-referenced ID. Materialize just this playback-only blank
        // slot; the canonical document and module header count stay unchanged.
        // In our MILKYTRACKER build XModule frees all 256 pattern buffers.
        auto data = std::make_unique<mp_ubyte[]>(64u * module.header.channum * 6u);
        destination.rows = 64;
        destination.effnum = 2;
        destination.channum = static_cast<mp_ubyte>(module.header.channum);
        destination.patternData = data.release();
    }
    void checkInstrument(int instrument) const {
        if (instrument < 1 || static_cast<std::size_t>(instrument) > sampleCounts.size())
            throw std::out_of_range("Playback instrument");
    }
    void mixAudition(mp_sint32* destination, mp_uint32 frames) {
        if (!audition.needsPreviewPattern()) {
            audition.mix(destination, frames);
            return;
        }
        // Song and audition mix sequentially under the same device lock.
        // Expose only this preview's scratch pattern while audition reads it;
        // source PCM/pattern cells and the canonical document stay untouched.
        // The first row contains the selected row, the second is silent. Once
        // the selected row ends, make both rows blank so Bxx/Dxx/E6x cannot
        // cause another note/effect to run before entering instrument-only idle.
        auto& pattern = module.phead[previewPattern];
        previewPatternData.fill(0);
        if (audition.isReadingPreviewRow()) {
            const auto rowBytes = static_cast<std::size_t>(pattern.channum) * 6;
            std::copy_n(pattern.patternData + previewRow * rowBytes, rowBytes, previewPatternData.begin());
            if (previewOverrideChannel >= 0)
                std::copy(previewOverride.begin(), previewOverride.end(), previewPatternData.begin() + previewOverrideChannel * 6);
        }
        struct RestorePattern {
            TXMPattern& destination;
            mp_uword rows;
            mp_ubyte* data;
            // TXMPattern::operator= owns/reallocates its patternData. Restore
            // only the two borrowed fields, never assign the pattern object.
            ~RestorePattern() { destination.rows = rows; destination.patternData = data; }
        } restore{pattern, pattern.rows, pattern.patternData};
        pattern.rows = 2;
        pattern.patternData = previewPatternData.data();
        audition.mix(destination, frames);
    }
    void reclaimRetired() {
        // Only called after all song, instrument-preview and raw-preview voices
        // have been reset, so no mixer can still reference these allocations.
        for (auto* retiredSample : retiredSamples) module.freeSampleMem(retiredSample);
        retiredSamples.clear();
        retiredBytes = 0;
    }
};

class DeviceLock {
public:
    DeviceLock(const SDL_AudioDeviceID& device, std::mutex& stateMutex)
        : appLock_(stateMutex), device_(device) {
        if (device_) SDL_LockAudioDevice(device_);
    }
    ~DeviceLock() { if (device_) SDL_UnlockAudioDevice(device_); }
private:
    std::unique_lock<std::mutex> appLock_;
    SDL_AudioDeviceID device_;
};

void validateNote(int note) {
    if (note < 1 || note > 96) throw std::out_of_range("XM preview note must be 1..96");
}

// Match LoaderXM's conversion into MilkyPlay's two effect slots. The document
// retains the original five XM bytes; these transformed bytes are playback-only.
std::array<mp_ubyte, 6> playbackCell(const Cell& cell) {
    if (cell.note > 97) throw std::out_of_range("XM cell note");
    std::array<mp_ubyte, 6> result{};
    result[0] = cell.note == 97 ? static_cast<mp_ubyte>(XModule::NOTE_OFF) : cell.note;
    result[1] = cell.instrument;
    XModule::convertXMVolumeEffects(cell.volume, result[2], result[3]);
    mp_ubyte effect = cell.effect, parameter = cell.param;
    if (std::find(XModule::validXMEffects, XModule::validXMEffects + XModule::numValidXMEffects,
                  effect) == XModule::validXMEffects + XModule::numValidXMEffects) {
        effect = parameter = 0;
    }
    if (effect == 0x0c || effect == 0x10) parameter = static_cast<mp_ubyte>(XModule::vol64to255(parameter));
    if (effect == 0 && parameter != 0) effect = 0x20;
    if (effect == 0x0e) { effect = static_cast<mp_ubyte>((parameter >> 4) + 0x30); parameter &= 15; }
    if (effect == 0x21) { effect = static_cast<mp_ubyte>((parameter >> 4) + 0x40); parameter &= 15; }
    result[4] = effect;
    result[5] = parameter;
    return result;
}

bool samePlaybackShape(const Document& a, const Document& b) {
    if (a.channels != b.channels || a.linear != b.linear || a.orders != b.orders ||
        a.speed != b.speed || a.bpm != b.bpm || a.restart != b.restart ||
        a.patterns.size() != b.patterns.size() || a.instruments.size() != b.instruments.size()) return false;
    for (std::size_t i = 0; i < a.patterns.size(); ++i)
        if (a.patterns[i].rows != b.patterns[i].rows || a.patterns[i].cells.size() != b.patterns[i].cells.size()) return false;
    for (std::size_t i = 0; i < a.instruments.size(); ++i)
        if (a.instruments[i].samples.size() != b.instruments[i].samples.size()) return false;
    return true;
}

bool sampleBufferChanged(const Sample& a, const Sample& b) {
    return a.sixteenBit != b.sixteenBit || a.loopType != b.loopType ||
        a.loopStart != b.loopStart || a.loopLength != b.loopLength || a.pcm != b.pcm;
}

class AtomicWav {
public:
    AtomicWav(const std::filesystem::path& destination, int sampleRate)
        : destination_(destination), temporary_(destination), sampleRate_(sampleRate) {
        if (destination.filename().empty()) throw std::invalid_argument("WAV filename is empty");
        static std::atomic<unsigned long> serial{0};
        temporary_ += ".render.tmp." + std::to_string(getpid()) + "." + std::to_string(++serial);
        file_ = ::open(temporary_.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (file_ < 0) throw std::runtime_error("Cannot create temporary WAV file");
        try { writeHeader(0); }
        catch (...) { closeAndRemove(); throw; }
    }
    ~AtomicWav() { closeAndRemove(); }
    void append(const std::int16_t* samples, std::size_t frames) {
        if (frames > maxBlockFrames) throw std::logic_error("WAV block is too large");
        std::array<std::uint8_t, maxBlockFrames * 4> bytes{};
        for (std::size_t i = 0; i < frames * 2; ++i) {
            const auto value = static_cast<std::uint16_t>(samples[i]);
            bytes[i * 2] = static_cast<std::uint8_t>(value);
            bytes[i * 2 + 1] = static_cast<std::uint8_t>(value >> 8);
        }
        write(bytes.data(), frames * 4);
        frames_ += frames;
    }
    void commit() {
        const auto bytes = frames_ * 4;
        if (bytes > std::numeric_limits<std::uint32_t>::max() - 36u)
            throw std::runtime_error("WAV exceeds RIFF's 4 GiB size limit");
        if (::lseek(file_, 0, SEEK_SET) < 0) throw std::runtime_error("Cannot finalize WAV header");
        writeHeader(static_cast<std::uint32_t>(bytes));
        if (::fsync(file_) != 0) throw std::runtime_error("Cannot flush WAV file");
        const auto closed = ::close(file_); file_ = -1;
        if (closed != 0) throw std::runtime_error("Cannot close WAV file");
        if (::rename(temporary_.c_str(), destination_.c_str()) != 0)
            throw std::runtime_error("Cannot replace destination WAV; previous file was preserved");
        committed_ = true;
    }
private:
    void write(const std::uint8_t* bytes, std::size_t size) {
        while (size) {
            const auto amount = ::write(file_, bytes, size);
            if (amount < 0 && errno == EINTR) continue;
            if (amount <= 0) throw std::runtime_error("Cannot write WAV audio");
            bytes += amount;
            size -= static_cast<std::size_t>(amount);
        }
    }
    void writeHeader(std::uint32_t dataBytes) {
        std::array<std::uint8_t, 44> header{};
        std::memcpy(header.data(), "RIFF", 4);
        std::memcpy(header.data() + 8, "WAVEfmt ", 8);
        std::memcpy(header.data() + 36, "data", 4);
        const auto word = [&](std::size_t offset, std::uint32_t value, int size) {
            for (int i = 0; i < size; ++i) header[offset + i] = static_cast<std::uint8_t>(value >> (i * 8));
        };
        word(4, dataBytes + 36, 4); word(16, 16, 4); word(20, 1, 2); word(22, 2, 2);
        word(24, sampleRate_, 4); word(28, sampleRate_ * 4, 4);
        word(32, 4, 2); word(34, 16, 2); word(40, dataBytes, 4);
        write(header.data(), header.size());
    }
    void closeAndRemove() noexcept {
        if (file_ >= 0) { ::close(file_); file_ = -1; }
        if (!committed_) ::unlink(temporary_.c_str());
    }
    std::filesystem::path destination_, temporary_;
    int sampleRate_;
    std::uint64_t frames_ = 0;
    bool committed_ = false;
    int file_ = -1;
};

} // namespace

struct Playback::Impl {
    // UI/worker document operations serialize here. The callback never takes
    // this mutex; large snapshots and codec work must stay outside DeviceLock.
    std::mutex documentMutex;
    // Serializes public control calls even before an SDL device exists. SDL's
    // device lock separately excludes its callback, which never takes a mutex.
    std::mutex stateMutex;
    SDL_AudioDeviceID device = 0;
    bool ownsAudioSubsystem = false;
    AudioSettings settings;
    std::unique_ptr<Core> core;
    std::array<mp_sint32, maxBlockFrames * 2> mixBuffer{};
    std::array<std::int16_t, maxBlockFrames * 2> outputBuffer{};
    std::size_t blockFrames;
    std::size_t bufferedOffset;
    bool bufferedSongPlaying = false;
    std::uint64_t elapsedFrames = 0;
    std::array<int, 2> outputPeaks{};
    std::array<bool, 2> outputClipped{};
    int soloChannel = -1;
    std::uint32_t mutedChannels = 0;
    std::uint64_t revision = 0;
    bool stopEndedVoices = true;

    explicit Impl(const AudioSettings& audioSettings)
        : settings(audioSettings), blockFrames(audioSettings.sampleRate / 250), bufferedOffset(blockFrames) {}
    ~Impl() {
        if (device) SDL_CloseAudioDevice(device);
        if (ownsAudioSubsystem) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
    // Returns a paused device. Commit the core/settings before unpausing it.
    SDL_AudioDeviceID openDevice(const AudioSettings& audioSettings) {
        // Hold our own reference so capture and output can close independently.
        if (!ownsAudioSubsystem) {
            if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) throw std::runtime_error(SDL_GetError());
            ownsAudioSubsystem = true;
        }
        SDL_AudioSpec wanted{}, obtained{};
        wanted.freq = audioSettings.sampleRate;
        wanted.format = AUDIO_S16SYS;
        wanted.channels = 2;
        wanted.samples = 512;
        wanted.callback = &Impl::callback;
        wanted.userdata = this;
        const auto opened = SDL_OpenAudioDevice(nullptr, 0, &wanted, &obtained, 0);
        if (!opened) throw std::runtime_error(std::string("SDL audio: ") + SDL_GetError());
        return opened;
    }
    Core& requireCore() {
        if (!core) throw std::runtime_error("No document loaded for playback");
        return *core;
    }
    void applyChannelMutes() {
        if (!core) return;
        // Dropping channels also drops their mute bits. Re-expanding later
        // must not unexpectedly silence a newly created channel.
        const int count = core->module.header.channum;
        if (soloChannel >= count) {
            soloChannel = -1;
            mutedChannels = 0;
        } else if (soloChannel >= 0) {
            mutedChannels = ~(std::uint32_t{1} << soloChannel);
        }
        if (count < 32) mutedChannels &= (std::uint32_t{1} << count) - 1;
        for (int channel = 0; channel < count; ++channel) {
            const bool muted = (mutedChannels & (std::uint32_t{1} << channel)) != 0;
            core->song.muteChannel(channel, muted);
            if (core->audition.isHeldRowPreview()) core->audition.muteChannel(channel, muted);
        }
    }
    void stopVoices() {
        if (core) {
            core->song.clearRowLoop();
            core->song.setIdle(true);
            core->song.resetChannelsFull();
            applyChannelMutes();
            core->song.clearMonitorNotes();
            core->audition.stopAudition();
            core->sample.resetChannelsFull();
            core->reclaimRetired();
        }
        bufferedOffset = blockFrames;
        bufferedSongPlaying = false;
    }
    void render(std::int16_t* destination, std::size_t frames) noexcept {
        while (frames) {
            if (bufferedOffset == blockFrames) {
                mixBuffer.fill(0);
                bufferedSongPlaying = false;
                if (core) {
                    core->song.mix(mixBuffer.data(), static_cast<mp_uint32>(blockFrames));
                    // A core block is exactly one native mixer packet. Its
                    // timer runs before mixing, so a song that halted does not
                    // contribute listening time.
                    bufferedSongPlaying = core->song.transport().playing;
                    if (stopEndedVoices && (core->song.hasSongHalted() || core->song.hasFinishedAudition()) &&
                        !core->endedVoicesStopped) {
                        for (int channel = 0; channel < core->module.header.channum; ++channel)
                            core->song.stopSample(channel);
                        core->endedVoicesStopped = true;
                    }
                    core->mixAudition(mixBuffer.data(), static_cast<mp_uint32>(blockFrames));
                    core->sample.mix(mixBuffer.data(), static_cast<mp_uint32>(blockFrames));
                }
                for (std::size_t i = 0; i < blockFrames * 2; ++i)
                    outputBuffer[i] = outputSample(mixBuffer[i]);
                bufferedOffset = 0;
            }
            const auto amount = std::min(frames, blockFrames - bufferedOffset);
            std::copy_n(outputBuffer.data() + bufferedOffset * 2, amount * 2, destination);
            // Meter only PCM actually handed to the output, not the rest of a
            // prepared block which a stop/edit may discard. mixBuffer still
            // holds the combined song + both previews before 16-bit clamping.
            for (std::size_t frame = bufferedOffset; frame < bufferedOffset + amount; ++frame) {
                for (std::size_t side = 0; side < 2; ++side) {
                    const auto index = frame * 2 + side;
                    outputPeaks[side] = std::max(outputPeaks[side],
                        std::abs(static_cast<int>(outputBuffer[index])));
                    outputClipped[side] = outputClipped[side] ||
                        mixBuffer[index] < outputMixMinimum || mixBuffer[index] > outputMixMaximum;
                }
            }
            // Only count the consumed part of the prepared block. Stop/edit
            // may discard its remainder, and previews share this output path.
            if (bufferedSongPlaying) elapsedFrames += amount;
            destination += amount * 2;
            bufferedOffset += amount;
            frames -= amount;
        }
    }
    static void callback(void* userdata, Uint8* stream, int length) noexcept {
        auto& self = *static_cast<Impl*>(userdata);
        std::memset(stream, 0, static_cast<std::size_t>(length));
        self.render(reinterpret_cast<std::int16_t*>(stream), static_cast<std::size_t>(length) / 4);
    }
};

Playback::Playback(const AudioSettings& settings) {
    validateAudioSettings(settings);
    impl_ = std::make_unique<Impl>(settings);
}
Playback::~Playback() = default;

void Playback::open() {
    std::lock_guard<std::mutex> stateLock(impl_->stateMutex);
    if (impl_->device) return;
    impl_->device = impl_->openDevice(impl_->settings);
    SDL_PauseAudioDevice(impl_->device, 0);
}

bool Playback::isOpen() const {
    std::lock_guard<std::mutex> stateLock(impl_->stateMutex);
    return impl_->device != 0;
}

AudioSettings Playback::audioSettings() const {
    std::lock_guard<std::mutex> stateLock(impl_->stateMutex);
    return impl_->settings;
}

void Playback::configure(const AudioSettings& settings) {
    validateAudioSettings(settings);
    std::lock_guard<std::mutex> documentLock(impl_->documentMutex);
    {
        std::lock_guard<std::mutex> stateLock(impl_->stateMutex);
        if (settings.sampleRate == impl_->settings.sampleRate &&
            settings.interpolation == impl_->settings.interpolation) return;
    }
    // Document changes/configure serialize on documentMutex. Codec and mixer
    // allocations happen before stopping playback or touching its device.
    auto replacement = impl_->core ? std::make_unique<Core>(impl_->core->document, settings) : nullptr;
    std::lock_guard<std::mutex> stateLock(impl_->stateMutex);
    const bool reopen = impl_->device != 0;
    if (reopen) SDL_PauseAudioDevice(impl_->device, 1);
    impl_->stopVoices();
    ++impl_->revision;
    impl_->outputPeaks.fill(0);
    impl_->outputClipped.fill(false);
    if (reopen) {
        // Close first: handheld backends may only allow one output device.
        // The old settings/core remain intact until the new device succeeds.
        SDL_CloseAudioDevice(impl_->device);
        impl_->device = 0;
        try {
            impl_->device = impl_->openDevice(settings);
        } catch (const std::exception& error) {
            const std::string failure = error.what();
            try {
                impl_->device = impl_->openDevice(impl_->settings);
            } catch (const std::exception& rollbackError) {
                throw std::runtime_error(failure + "; previous audio device could not reopen: " + rollbackError.what());
            }
            SDL_PauseAudioDevice(impl_->device, 0);
            throw std::runtime_error(failure + "; previous audio settings restored");
        }
    }
    impl_->core.swap(replacement);
    impl_->settings = settings;
    impl_->blockFrames = settings.sampleRate / 250;
    impl_->bufferedOffset = impl_->blockFrames;
    impl_->elapsedFrames = 0;
    impl_->applyChannelMutes();
    if (reopen) SDL_PauseAudioDevice(impl_->device, 0);
}

void Playback::load(const Document& document) {
    std::lock_guard<std::mutex> documentLock(impl_->documentMutex);
    auto replacement = std::make_unique<Core>(document, impl_->settings);
    {
        DeviceLock lock(impl_->device, impl_->stateMutex);
        impl_->core.swap(replacement);
        ++impl_->revision;
        impl_->bufferedOffset = impl_->blockFrames;
        impl_->elapsedFrames = 0;
        impl_->applyChannelMutes();
    }
    // Old sample memory is freed after the callback can no longer reference it.
}

DocumentUpdateResult Playback::updateDocument(const Document& document) {
    std::lock_guard<std::mutex> documentLock(impl_->documentMutex);
    const Document* previous = impl_->core ? &impl_->core->document : nullptr;
    std::uint64_t revision = 0;
    bool hadCore = false;
    {
        DeviceLock lock(impl_->device, impl_->stateMutex);
        hadCore = static_cast<bool>(impl_->core);
        revision = impl_->revision;
    }
    auto replacement = std::make_unique<Core>(document, impl_->settings);
    bool compatible = hadCore && samePlaybackShape(*previous, document);
    std::array<bool, 2048> replaceBuffer{};
    std::array<bool, 256> replacePattern{};
    std::size_t additionalRetiredBytes = 0, additionalRetiredCount = 0;
    if (compatible) {
        for (std::size_t i = 0; i < document.patterns.size(); ++i) {
            const auto& oldCells = previous->patterns[i].cells;
            const auto& newCells = document.patterns[i].cells;
            replacePattern[i] = !std::equal(oldCells.begin(), oldCells.end(), newCells.begin(),
                [](const Cell& a, const Cell& b) {
                    return a.note == b.note && a.instrument == b.instrument && a.volume == b.volume &&
                        a.effect == b.effect && a.param == b.param;
                });
        }
        for (std::size_t i = 0; i < document.instruments.size(); ++i) {
            for (std::size_t s = 0; s < document.instruments[i].samples.size(); ++s) {
                const auto& oldSample = previous->instruments[i].samples[s];
                replaceBuffer[i * 16 + s] = sampleBufferChanged(oldSample, document.instruments[i].samples[s]);
                if (replaceBuffer[i * 16 + s] && !oldSample.pcm.empty()) {
                    additionalRetiredBytes += oldSample.pcm.size() * (oldSample.sixteenBit ? 2u : 1u) + 64;
                    ++additionalRetiredCount;
                }
            }
        }
    }
    DocumentUpdateResult result = DocumentUpdateResult::IdleReloaded;
    {
        DeviceLock lock(impl_->device, impl_->stateMutex);
        if (revision != impl_->revision) throw std::runtime_error("Document changed while preparing the audio update");
        if (compatible) {
            const auto& current = *impl_->core;
            compatible = current.module.numVEnvs == replacement->module.numVEnvs &&
                current.module.numPEnvs == replacement->module.numPEnvs &&
                current.retiredBytes + additionalRetiredBytes <= 64u * 1024u * 1024u &&
                current.retiredSamples.size() + additionalRetiredCount <= 1024;
        }
        if (compatible) {
            auto& current = *impl_->core;
            auto& prepared = replacement->module;
            // Keep XModule and envelope addresses stable. Active sample voices
            // retain their old PCM until retrigger/stop; new notes use new PCM.
            // Retired allocations remain in this module's pool and cannot dangle.
            if (prepared.numVEnvs) std::copy_n(prepared.venvs, prepared.numVEnvs, current.module.venvs);
            if (prepared.numPEnvs) std::copy_n(prepared.penvs, prepared.numPEnvs, current.module.penvs);
            std::copy_n(prepared.instr, 256, current.module.instr);
            for (std::size_t index = 0; index < prepared.header.smpnum; ++index) {
                auto* oldPointer = current.module.smp[index].sample;
                auto* newPointer = prepared.smp[index].sample;
                current.module.smp[index] = prepared.smp[index];
                if (index < replaceBuffer.size() && replaceBuffer[index]) {
                    if (oldPointer) current.retiredSamples.push_back(reinterpret_cast<mp_ubyte*>(oldPointer));
                    if (newPointer) {
                        prepared.removeSamplePtr(reinterpret_cast<mp_ubyte*>(newPointer));
                        current.module.insertSamplePtr(reinterpret_cast<mp_ubyte*>(newPointer));
                    }
                } else {
                    current.module.smp[index].sample = oldPointer;
                }
            }
            current.retiredBytes += additionalRetiredBytes;
            for (int pattern = 0; pattern < prepared.header.patnum; ++pattern) {
                if (!replacePattern[pattern]) continue;
                const auto& source = prepared.phead[pattern];
                auto& destination = current.module.phead[pattern];
                const auto bytes = static_cast<std::size_t>(source.rows) * source.channum * (2 + source.effnum * 2);
                if (bytes && source.patternData && destination.patternData)
                    std::memcpy(destination.patternData, source.patternData, bytes);
            }
            current.module.header = prepared.header;
            std::swap(current.document, replacement->document);
            result = DocumentUpdateResult::Preserved;
        } else {
            const auto old = impl_->core ? impl_->core->song.transport() : PlaybackStatus{};
            if (old.playing) {
                const int order = std::clamp(old.order, 0, static_cast<int>(replacement->module.header.ordnum) - 1);
                const int oldPatternMode = impl_->core->song.getPatternToPlay();
                const int pattern = oldPatternMode >= 0 && oldPatternMode < 256
                    ? oldPatternMode : replacement->module.header.ord[order];
                replacement->preparePattern(pattern);
                const int rows = replacement->module.phead[pattern].rows;
                const bool rowLoop = oldPatternMode >= 0 && old.loopFirstRow >= 0;
                const int first = rowLoop ? std::min(old.loopFirstRow, rows - 1) : 0;
                const int end = rowLoop ? std::clamp(old.loopEndRow, first + 1, rows) : rows;
                const int row = std::clamp(old.row, first, end - 1);
                replacement->song.setPatternToPlay(oldPatternMode >= 0 ? pattern : -1);
                replacement->song.setRepeat(oldPatternMode >= 0);
                // Structural edits may replace the core during a row preview.
                // Retain its one-row limit instead of starting a pattern loop.
                replacement->song.restart(order, row, true, nullptr, old.rowAudition);
                if (rowLoop) replacement->song.setRowLoop(first, end);
                replacement->song.setIdle(false);
                result = DocumentUpdateResult::Restarted;
            }
            impl_->core.swap(replacement);
            impl_->bufferedOffset = impl_->blockFrames;
            impl_->applyChannelMutes();
        }
        ++impl_->revision;
    }
    return result;
}

void Playback::playSong(int order, int row) {
    DeviceLock lock(impl_->device, impl_->stateMutex);
    auto& core = impl_->requireCore();
    if (order < 0 || order >= core.module.header.ordnum) throw std::out_of_range("Playback order");
    core.checkPattern(core.module.header.ord[order], row);
    core.song.setPatternToPlay(-1);
    core.song.setRepeat(false);
    core.endedVoicesStopped = false;
    core.song.restart(order, row, true);
    core.song.setIdle(false);
    impl_->applyChannelMutes();
    impl_->bufferedOffset = impl_->blockFrames;
    impl_->elapsedFrames = 0;
    ++impl_->revision;
}

void Playback::playPattern(int pattern, int row, bool oneRow, int loopEndRow, int loopFirstRow) {
    DeviceLock lock(impl_->device, impl_->stateMutex);
    auto& core = impl_->requireCore();
    core.checkPattern(pattern, row);
    const int first = loopFirstRow < 0 ? row : loopFirstRow;
    if (loopEndRow >= 0) {
        if (oneRow || first > row || loopEndRow <= row) throw std::invalid_argument("Invalid pattern loop range");
        core.checkPattern(pattern, first);
        core.checkPattern(pattern, loopEndRow - 1);
    } else if(loopFirstRow >= 0)throw std::invalid_argument("Pattern loop requires an end row");
    core.preparePattern(pattern);
    core.song.setPatternToPlay(pattern);
    core.song.setRepeat(true);
    core.endedVoicesStopped = false;
    core.song.restart(0, row, true, nullptr, oneRow);
    if (loopEndRow >= 0) core.song.setRowLoop(first, loopEndRow);
    core.song.setIdle(false);
    impl_->applyChannelMutes();
    impl_->bufferedOffset = impl_->blockFrames;
    impl_->elapsedFrames = 0;
    ++impl_->revision;
}

void Playback::stop() {
    DeviceLock lock(impl_->device, impl_->stateMutex);
    if (!impl_->core) return;
    impl_->stopVoices();
    ++impl_->revision;
}

void Playback::previewNote(int instrumentOneBased, int noteXm) {
    validateNote(noteXm);
    DeviceLock lock(impl_->device, impl_->stateMutex);
    auto& core = impl_->requireCore();
    core.checkInstrument(instrumentOneBased);
    if (core.audition.isHeldRowPreview()) core.audition.stopAudition();
    core.audition.playNote(0, noteXm, instrumentOneBased);
}

void Playback::previewRow(int pattern, int row, int overrideChannel, const Cell* overrideCell) {
    const auto converted = overrideCell ? playbackCell(*overrideCell) : std::array<mp_ubyte, 6>{};
    DeviceLock lock(impl_->device, impl_->stateMutex);
    auto& core = impl_->requireCore();
    core.checkPattern(pattern, row);
    if ((overrideCell && (overrideChannel < 0 || overrideChannel >= core.module.header.channum)) ||
        (!overrideCell && overrideChannel != -1))
        throw std::out_of_range("Row preview override channel");
    core.preparePattern(pattern);
    const auto& data = core.module.phead[pattern];
    if (data.effnum != 2 || data.channum != core.module.header.channum)
        throw std::runtime_error("Row preview pattern layout");
    core.audition.setPatternToPlay(pattern);
    core.audition.setRepeat(true);
    core.audition.restart(0, 0, true);
    core.previewPattern = pattern;
    core.previewRow = row;
    core.previewOverrideChannel = overrideChannel;
    core.previewOverride = converted;
    core.audition.beginHeldRowPreview();
    impl_->applyChannelMutes();
    // Do not discard a partially delivered song block. Stopped previews can
    // replace the old preview's buffered sound immediately on a new trigger.
    if (!core.song.transport().playing) impl_->bufferedOffset = impl_->blockFrames;
}

void Playback::previewSample(int instrumentOneBased, int sampleIndex, int noteXm, int start, int end) {
    validateNote(noteXm);
    DeviceLock lock(impl_->device, impl_->stateMutex);
    auto& core = impl_->requireCore();
    core.checkInstrument(instrumentOneBased);
    if (sampleIndex < 0 || static_cast<std::size_t>(sampleIndex) >= core.sampleCounts[instrumentOneBased - 1])
        throw std::out_of_range("Playback sample");
    const auto& sample = core.module.smp[(instrumentOneBased - 1) * 16 + sampleIndex];
    if (!sample.sample || sample.samplen == 0) return;
    const auto sampleLength = static_cast<int>(sample.samplen);
    const auto rangeEnd = end < 0 ? sampleLength : end;
    if (start < 0 || start >= rangeEnd || rangeEnd > sampleLength)
        throw std::out_of_range("Sample preview range");
    const double semitones = noteXm - 49 + sample.relnote + sample.finetune / 128.0;
    const int frequency = static_cast<int>(std::clamp(8363.0 * std::exp2(semitones / 12.0), 1.0, 384000.0));
    core.sample.setFreq(0, frequency);
    core.sample.setVol(0, (sample.vol * 512) / 255);
    core.sample.setPan(0, sample.pan);
    int flags = (sample.type & 16) ? 4 : 0;
    int loopStart = 0, loopEnd = rangeEnd;
    if (start == 0 && end < 0) {
        flags |= sample.type & 3;
        if (flags & 3) { loopStart = sample.loopstart; loopEnd = sample.loopstart + sample.looplen; }
    }
    core.sample.playSample(0, sample.sample, sampleLength, start, 0, false, loopStart, loopEnd, flags);
}

void Playback::stopPreview() {
    DeviceLock lock(impl_->device, impl_->stateMutex);
    if (!impl_->core) return;
    impl_->core->audition.stopAudition();
    impl_->core->sample.resetChannelsFull();
    if (impl_->core->song.isRowAudition()) {
        impl_->core->song.setIdle(true);
        impl_->core->song.resetChannelsFull();
        impl_->applyChannelMutes();
    }
    impl_->bufferedOffset = impl_->blockFrames;
}

void Playback::setSolo(int channel) {
    DeviceLock lock(impl_->device, impl_->stateMutex);
    auto& core = impl_->requireCore();
    if (channel < -1 || channel >= core.module.header.channum) throw std::out_of_range("Solo channel");
    impl_->soloChannel = channel;
    impl_->mutedChannels = channel < 0 ? 0 : ~(std::uint32_t{1} << channel);
    impl_->applyChannelMutes();
}

void Playback::setMuted(int channel, bool muted) {
    DeviceLock lock(impl_->device, impl_->stateMutex);
    const auto& core = impl_->requireCore();
    if (channel < 0 || channel >= core.module.header.channum) throw std::out_of_range("Mute channel");
    const auto bit = std::uint32_t{1} << channel;
    if (muted) impl_->mutedChannels |= bit;
    else impl_->mutedChannels &= ~bit;
    impl_->soloChannel = -1;
    impl_->applyChannelMutes();
}

void Playback::setMuteMask(std::uint32_t channels) {
    DeviceLock lock(impl_->device, impl_->stateMutex);
    impl_->mutedChannels = channels;
    impl_->soloChannel = -1;
    impl_->applyChannelMutes();
}

std::uint32_t Playback::muteMask() const {
    DeviceLock lock(impl_->device, impl_->stateMutex);
    return impl_->mutedChannels;
}

void Playback::setCell(int pattern, int row, int channel, const Cell& cell) {
    std::lock_guard<std::mutex> documentLock(impl_->documentMutex);
    const auto converted = playbackCell(cell);
    DeviceLock lock(impl_->device, impl_->stateMutex);
    auto& core = impl_->requireCore();
    core.checkPattern(pattern, row);
    auto& data = core.module.phead[pattern];
    if (channel < 0 || channel >= data.channum || data.effnum != 2 || !data.patternData)
        throw std::out_of_range("Playback cell layout");
    std::copy(converted.begin(), converted.end(), data.patternData + (row * data.channum + channel) * 6);
    if (static_cast<std::size_t>(pattern) < core.document.patterns.size())
        core.document.patterns[pattern].at(row, channel, core.document.channels) = cell;
    ++impl_->revision;
}

PlaybackStatus Playback::status() const {
    DeviceLock lock(impl_->device, impl_->stateMutex);
    auto result = impl_->core ? impl_->core->song.transport() : PlaybackStatus{};
    result.elapsedSeconds = static_cast<double>(impl_->elapsedFrames) / impl_->settings.sampleRate;
    return result;
}

PlaybackMonitor Playback::monitor() const {
    PlaybackMonitor result;
    DeviceLock lock(impl_->device, impl_->stateMutex);
    if (!impl_->core) return result;
    auto& core = *impl_->core;
    auto& song = core.song;
    result.channelCount = std::min<int>(core.module.header.channum, static_cast<int>(result.channels.size()));
    const bool playing = song.transport().playing;
    std::array<mp_sint32, 64> scope{};
    // Upstream computes smpadd * fMul in signed 32-bit arithmetic. For this
    // XM-only adapter, linear frequency is at most 535232 Hz. At 48 kHz,
    // smpadd <= 730766 and a 768-frame window stays below INT32_MAX. The Amiga
    // converter can reach INT32_MAX >> 8, requiring a 128-frame window instead.
    // Scale windows down with output rate so the bound and time span remain.
    // XM has no frequency envelope/freqadjust that could raise these bounds.
    const mp_sint32 scopeFrames = ((core.module.header.freqtab & 1) ? 768 : 128) *
        impl_->settings.sampleRate / Playback::sampleRate;
    for (int index = 0; index < result.channelCount; ++index) {
        auto& output = result.channels[static_cast<std::size_t>(index)];
        output.muted = song.isChannelMuted(index);
        output.active = playing && song.isChannelPlaying(index);
        if (!output.active) continue;
        TPlayerChannelInfo info{};
        song.grabChannelInfo(index, info);
        output.note = song.monitorNote(index);
        output.instrument = info.instrument;
        if (output.muted) continue;
        // This const upstream scope routine works on a local mixer-channel
        // copy; it does not advance the real voice or touch its effect memory.
        // Spread the fixed 64 points over the safely bounded source window.
        song.mixData(index, scope.data(), static_cast<mp_sint32>(scope.size()), 0, scopeFrames);
        int peak = 0;
        for (std::size_t point = 0; point < scope.size(); ++point) {
            const int value = std::clamp(scope[point], -32768, 32767);
            output.waveform[point] = static_cast<std::int16_t>(value);
            peak = std::max(peak, std::abs(value));
        }
        output.peak = static_cast<float>(peak) / 32768.0f;
    }
    return result;
}

OutputMeter Playback::takeOutputMeter() {
    DeviceLock lock(impl_->device, impl_->stateMutex);
    OutputMeter result;
    for (std::size_t side = 0; side < 2; ++side) {
        result.peak[side] = static_cast<float>(impl_->outputPeaks[side]) / 32768.0f;
        result.clipped[side] = impl_->outputClipped[side];
    }
    impl_->outputPeaks.fill(0);
    impl_->outputClipped.fill(false);
    return result;
}

void Playback::readFrames(std::int16_t* stereo, std::size_t frames) {
    DeviceLock lock(impl_->device, impl_->stateMutex);
    if (impl_->device) throw std::logic_error("Headless readFrames cannot run with an SDL output device");
    if (!stereo && frames) throw std::invalid_argument("Null audio output buffer");
    impl_->render(stereo, frames);
}

RenderResult renderWav(const Document& document, const std::filesystem::path& path,
                       const RenderOptions& options,
                       const std::function<bool(const RenderProgress&)>& progress) {
    validateAudioSettings(options.audioSettings);
    const int outputRate = options.audioSettings.sampleRate;
    const double maxRiffSeconds = (std::numeric_limits<std::uint32_t>::max() - 36u) / (outputRate * 4.0);
    if (!std::isfinite(options.maxSeconds) || options.maxSeconds <= 0 || options.maxSeconds > maxRiffSeconds ||
        !std::isfinite(options.tailSeconds) || options.tailSeconds < 0 || options.tailSeconds > 60 ||
        options.repeats < 1 || options.repeats > 1000)
        throw std::invalid_argument("Invalid WAV duration, tail (0..60 s), or song pass count (1..1000)");
    const bool orderRange = options.lastOrder != -1;
    if (orderRange && (options.firstOrder < 0 || options.lastOrder < options.firstOrder ||
        static_cast<std::size_t>(options.lastOrder) >= document.orders.size()))
        throw std::invalid_argument("Invalid WAV Order range");
    Playback playback(options.audioSettings);
    playback.load(document);
    playback.impl_->stopEndedVoices = false;
    AtomicWav output(path, outputRate);
    RenderResult result;
    const auto limit = static_cast<std::size_t>(options.maxSeconds * outputRate);
    const auto blockFrames = playback.impl_->blockFrames;
    std::array<std::int16_t, maxBlockFrames * 2> buffer{};
    std::size_t rendered = 0, nextProgress = 0;
    const auto notify = [&]() {
        const auto state = playback.status();
        result.seconds = static_cast<double>(rendered) / outputRate;
        if (progress && !progress({result.seconds, state.order, state.row})) {
            result.cancelled = true;
            return false;
        }
        nextProgress = rendered + outputRate / 4;
        return true;
    };
    if (!notify()) return result;
    for (int pass = 0; pass < options.repeats && !result.limitReached; ++pass) {
        playback.playSong(orderRange ? options.firstOrder : 0);
        if (orderRange) playback.impl_->core->song.setRenderOrderRange(options.firstOrder, options.lastOrder);
        while (playback.status().playing && rendered < limit) {
            const auto amount = std::min(blockFrames, limit - rendered);
            playback.readFrames(buffer.data(), amount);
            // The range guard suppresses this whole mixer packet before
            // mixing. It is a boundary marker, not part of the exported PCM.
            if (orderRange && playback.impl_->core->song.renderOrderRangeEnded()) break;
            output.append(buffer.data(), amount);
            rendered += amount;
            if (rendered >= nextProgress && !notify()) return result;
        }
        if (rendered >= limit) result.limitReached = true;
    }
    if (!result.limitReached && options.tailSeconds > 0) {
        playback.impl_->core->song.releaseForTail();
        const auto tailFrames = static_cast<std::size_t>(options.tailSeconds * outputRate);
        const auto tailEnd = rendered + tailFrames;
        while (rendered < tailEnd && rendered < limit) {
            const auto amount = std::min({blockFrames, tailEnd - rendered, limit - rendered});
            playback.readFrames(buffer.data(), amount);
            output.append(buffer.data(), amount);
            rendered += amount;
            if (rendered >= nextProgress && !notify()) return result;
        }
        result.limitReached = rendered < tailEnd;
    }
    if (!notify()) return result;
    output.commit();
    return result;
}

void renderWav(const Document& document, const std::filesystem::path& path, double maxSeconds) {
    RenderOptions options;
    options.maxSeconds = maxSeconds;
    options.tailSeconds = 0;
    (void)renderWav(document, path, options);
}

} // namespace brick
