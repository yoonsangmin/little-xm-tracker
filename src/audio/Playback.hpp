#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>

namespace brick {

struct Document;
struct Cell;
struct Sample;

struct PlaybackStatus {
    bool playing = false;
    // True only while a one-row audition is actively running. It is false
    // after completion/cancellation and during ordinary song/pattern playback.
    bool rowAudition = false;
    int order = 0;
    int pattern = 0;
    int row = 0;
    int bpm = 125;
    int speed = 6;
    // Song/pattern PCM delivered since the last explicit playback start.
    // Stops freeze it; previews do not add time.
    double elapsedSeconds = 0.0;
    // Session-only pattern loop, [first, end). -1 means ordinary playback.
    int loopFirstRow = -1;
    int loopEndRow = -1;
};

struct ChannelMonitor {
    int note = 0;       // XM note 1..96, or 0 when there is no active voice.
    int instrument = 0; // One-based XM instrument.
    bool active = false;
    bool muted = false;
    float peak = 0.0f; // Scope peak normalized to 0..1.
    std::array<std::int16_t, 64> waveform{};
};
struct PlaybackMonitor {
    int channelCount = 0;
    std::array<ChannelMonitor, 32> channels{};
};

struct SamplePreviewStatus {
    bool active = false;
    int instrument = 0; // One-based instrument, or 0 for an unsaved draft.
    int sample = -1;    // Zero-based sample slot, or -1 for an unsaved draft.
    std::size_t frame = 0;
};

struct OutputMeter {
    // Final delivered stereo PCM peaks: abs(sample) / 32768, in 0..1.
    std::array<float, 2> peak{};
    // The summed mix exceeded signed 16-bit range before output clamping.
    std::array<bool, 2> clipped{};
};

enum class DocumentUpdateResult { Preserved, Restarted, IdleReloaded };

enum class Interpolation { Nearest, Linear, CubicLagrange, CubicSpline, Sinc, SincPrecise };
struct AudioSettings {
    int sampleRate = 48000; // Supported output rates: 44100 and 48000 Hz.
    Interpolation interpolation = Interpolation::Linear;
};

struct RenderOptions {
    double maxSeconds = 1800.0;
    double tailSeconds = 2.0;
    int repeats = 1; // Total passes; each starts fresh at the selected beginning.
    // -1 renders the whole song, ignoring firstOrder. Otherwise both endpoints
    // are inclusive Order positions. Start at row 0 without earlier state;
    // stop before the first row outside the range, or at the engine's song end.
    int firstOrder = 0;
    int lastOrder = -1;
    AudioSettings audioSettings{};
};
struct RenderProgress { double seconds = 0.0; int order = 0; int row = 0; };
struct RenderResult { double seconds = 0.0; bool cancelled = false; bool limitReached = false; };

// App-owned adapter. Upstream MilkyPlay sources remain unchanged.
class Playback {
public:
    explicit Playback(const AudioSettings& settings = {});
    ~Playback();
    Playback(const Playback&) = delete;
    Playback& operator=(const Playback&) = delete;

    void open();
    bool isOpen() const;
    AudioSettings audioSettings() const;
    // Changed settings stop song/previews and reset their elapsed time. Preserve
    // the document and channel mute/solo choices. An open output device is
    // reopened; failures restore the previous settings and attempt to reopen
    // them. isOpen() reports rollback failure. Equal settings are a no-op;
    // headless playback stays headless.
    void configure(const AudioSettings& settings);
    void load(const Document& document);
    DocumentUpdateResult updateDocument(const Document& document);
    void playSong(int order = 0, int row = 0);
    // loopEndRow limits repetition to [loopFirstRow, loopEndRow); all channels
    // play. An omitted first row uses the starting row as the loop beginning.
    // A negative end keeps ordinary whole-pattern/one-row playback.
    void playPattern(int pattern, int row = 0, bool oneRow = false, int loopEndRow = -1, int loopFirstRow = -1);
    void stop();
    void previewNote(int instrumentOneBased, int noteXm);
    // Trigger one row from fresh state on the independent preview player.
    // After its row effects finish, instrument envelopes/auto-vibrato/fade keep
    // ticking until stopPreview(); no next row or repeated row is processed.
    // The optional copied cell substitutes only this preview, never the song
    // or document. Row previews follow the current channel mute/solo mask.
    void previewRow(int pattern, int row, int overrideChannel = -1, const Cell* overrideCell = nullptr);
    void previewSample(int instrumentOneBased, int sampleIndex,
                       int noteXm = 49, int start = 0, int end = -1);
    // Audition an owned snapshot of unsaved PCM without inserting a sample or
    // changing the loaded document. Refresh restarts this raw preview; loops,
    // tuning, volume and panning match the committed sample preview path.
    void previewSample(const Sample& sample, int noteXm = 49);
    void stopPreview();
    // Active raw/instrument voices, or a row preview awaiting its next tick.
    // Song transport is separate. Finished one-shots return false; looping
    // voices remain active until stopped even when their volume is zero.
    bool previewPlaying() const;
    // Raw sample audition only; song and instrument voices are not included.
    // Frame is absolute in the original PCM, including selected-range previews.
    // This mixer snapshot can lead audible output by the audio buffer latency.
    SamplePreviewStatus samplePreviewStatus() const;
    // Solo replaces the current mask with all channels except this one muted.
    // -1 clears every mute. The UI toggles an already-selected Solo with -1.
    void setSolo(int channel = -1);
    // Session-only effective channel mask; bit 0 is channel 1. Editing it
    // cancels the Solo marker. Instrument/sample previews remain independent;
    // row previews use the mask. New-file callers reset it with setMuteMask(0).
    void setMuted(int channel, bool muted);
    void setMuteMask(std::uint32_t channels);
    std::uint32_t muteMask() const;
    void setCell(int pattern, int row, int channel, const Cell& cell);
    PlaybackStatus status() const;
    // Call at most 30 Hz while the channel monitor is visible. Uses fixed
    // buffers under a short device lock; waveform rendering stays on the UI
    // thread. The player updates a fixed last-note cache after each audio tick.
    // MilkyPlay mixData scopes the actual mixer PCM/position/pitch/loop/volume
    // snapshot. It is not a capture of final stereo/ramping/filter output.
    // 64 points span about 16 ms for linear XM, 2.67 ms for Amiga-frequency XM;
    // the latter is bounded to avoid upstream's signed scope-rate overflow.
    // Muted and stopped channels produce zero waveform/peak.
    PlaybackMonitor monitor() const;

    // Consume the peaks and clipping latches since the previous call. Includes
    // song and both preview paths, after their actual stereo mix. Unconsumed
    // buffered frames are excluded. No audio means zeros.
    // UI decay/peak hold belongs to the caller; this call never advances audio.
    OutputMeter takeOutputMeter();

    // Headless pull mode: available until open() creates an SDL output device.
    // The same core rendering function serves the SDL callback and WAV export.
    void readFrames(std::int16_t* interleavedStereo, std::size_t frames);
    static constexpr int sampleRate = 48000; // Legacy default; use audioSettings() for the active rate.

private:
    friend RenderResult renderWav(const Document&, const std::filesystem::path&,
                                  const RenderOptions&,
                                  const std::function<bool(const RenderProgress&)>&);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

void renderWav(const Document& document, const std::filesystem::path& path,
               double maxSeconds = 120.0);
RenderResult renderWav(const Document& document, const std::filesystem::path& path,
                       const RenderOptions& options,
                       const std::function<bool(const RenderProgress&)>& progress = {});

} // namespace brick
