#pragma once

#include "model/SampleTools.hpp"

namespace brick::edit {

enum class WaveDistortion { Off, Clip, Fold, Wrap };
enum class WaveFilter { Off, LowPass, HighPass, BandPass, AllPass };
enum class WavePhase { Pinch, Warp, Resync, ResyncRaw };

struct WaveSynthOptions {
    WavePreset preset = WavePreset::Sine;
    NoisePreset noise = NoisePreset::Long;
    std::size_t frames = 32; // Tone period: 16, 32, 64, 128 or 256.
    int dutyEighths = 4;    // Pulse: 1, 2, 4 or 6 eighths.
    int levels = 256;      // Tones: 16 or 256. Noise retains normal PCM depth.
    WaveDistortion distortion = WaveDistortion::Off;
    int drivePercent = 100; // 0..800%, before filtering/distortion.
    int limitPercent = 100; // 1..100% of the original peak; ignored with DIST OFF.
    WaveFilter filter = WaveFilter::Off;
    int cutoffPercent = 25; // 1..99% of the generated sample's Nyquist frequency.
    int resonanceTenths = 7; // Standard biquad Q: 0.1..10.0.
    int vshiftPercent = 0; // -100..100% vertical bias before distortion.
    WavePhase phase = WavePhase::Pinch;
    int phaseAmount = 0; // 0..100; zero is unchanged. Ignored for noise.
};

// A fresh, ordinary eight-bit XM sample with a full forward loop. Generation
// does not run in the playback engine and does not retain synth parameters.
// Processing: shape/phase -> drive -> periodic filter -> vertical bias ->
// distortion/limit -> level quantization -> PCM. Final PCM always saturates
// safely, including DIST OFF. Default options preserve the original presets.
// Sixteen levels use the existing symmetric mid-rise alphabet (no zero step);
// an entirely silent signal is retained as zero. LIMIT is measured before final
// eight-bit rounding, which may differ by up to half a PCM step (128 units).
// RESYNC uses oversampling and circular low-pass decimation to reduce aliasing;
// RESYN2 (ResyncRaw) intentionally keeps the raw sampled waveform. These are
// app-owned shaping modes inspired by LSDj, not a bit-identical emulation.
Sample synthesizeWave(const WaveSynthOptions& options);

} // namespace brick::edit
