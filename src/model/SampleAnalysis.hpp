#pragma once

#include "model/EditOperations.hpp"

namespace brick::edit {

struct PitchEstimate {
    bool reliable = false;
    double frequency = 0.0; // Hz when this sample is played at C-4 with its current tuning.
    double confidence = 0.0; // 0..1 periodicity/stability score; not a probability.
    double cents = 0.0; // Difference from the nearest equal-tempered note.
    int note = 0; // FT2 note: 1=C-0, 49=C-4. Zero when no reliable note was found.
    int relativeNote = 0, finetune = 0; // Suggested tuning: C-4 input produces musical C-4.
};

// Analyze a half-open PCM range without changing the sample. Intended for a
// stable, single pitched sound, about 50..2500 Hz at its current C-4 rate.
// Silence, insufficient/unstable evidence, or unrepresentable tuning returns
// reliable=false. Invalid ranges throw. Work is bounded independently of sample
// length by inspecting a fixed number of short windows across the selection.
// When the selection exactly matches an enabled forward-loop region, analysis
// may repeat only that region to measure short single-cycle tracker waveforms.
PitchEstimate detectSamplePitch(const Sample& sample, FrameRange range);

} // namespace brick::edit
