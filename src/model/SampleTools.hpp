#pragma once

#include "model/EditOperations.hpp"

namespace brick::edit {

// The selection crosses the first point of a forward loop. Its counterpart,
// shifted by loopLength, must also fit in the PCM. Both regions are blended;
// the sample length, loop markers and tuning are retained.
void crossfadeLoop(Sample& sample, FrameRange selection);

// FT2-style whole-sample nearest-neighbour resampling. Positive semitones
// increase PCM length/rate; relativeNote changes equally to retain playback
// pitch/duration. Rejects a tuning or memory overflow instead of truncating.
void resampleSample(Sample& sample, int semitones);

// Mix clipboard PCM from start, extending the sample if needed. The source
// balance is 0..100%; outside the overlapping destination region is retained.
// Destination bit depth and all sample metadata are retained.
void mixPcm(Sample& sample, std::size_t start, const PcmClip& clip, int sourcePercent = 50);

// Set one canonical signed PCM value, quantized to the destination bit depth.
void drawPcmPoint(Sample& sample, std::size_t frame, int amplitude);

// Move one coarse control point, linearly joining the original left/right
// anchors. The inclusive endpoints must be in PCM; equal endpoints permit a
// single-frame edit. No length, loop, tuning or bit-depth changes occur.
void drawPcmStroke(Sample& sample, std::size_t left, std::size_t frame,
                   std::size_t right, int amplitude);

enum class WavePreset { Sine, Triangle, Square, Saw, Noise };
// Fill one period (or deterministic noise) into a half-open selection. PCM
// outside the range and every metadata field are retained. This is a one-time
// PCM edit, not a synthesizer or a voice running in the playback engine.
void fillWavePreset(Sample& sample, FrameRange range, WavePreset preset,
                    int amplitude = 16384, std::uint32_t noiseSeed = 0x6d2b79f5u);

// Whole-sample delay taps. Each echo retains decayPercent of the previous
// tap's level. repeats is 0..64, decayPercent is 0..100, delay is in PCM frames.
// extendTail keeps all taps; otherwise output remains the original length.
// Rejects work above 128M contributing taps as well as output above 32M frames.
void echoSample(Sample& sample, std::size_t delayFrames, int repeats,
                int decayPercent, bool extendTail);

// Fill slots strictly between two existing PCM endpoints with progressive
// blends. The endpoints must have equal length, tuning and loop geometry.
// Intermediate sample metadata follows the first endpoint; sixteen-bit data
// is retained if either endpoint is sixteen-bit. Key mappings/envelopes and
// both endpoints remain unchanged. Overwritten slots belong to the same XI.
// The combined generated intermediate PCM is capped at 32M frames.
void morphSamples(Instrument& instrument, std::size_t firstSlot, std::size_t lastSlot);

} // namespace brick::edit
