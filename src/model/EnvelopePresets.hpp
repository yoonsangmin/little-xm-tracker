#pragma once

#include "model/Document.hpp"

namespace brick::edit {

constexpr std::size_t EnvelopePresetCount = 6;

struct EnvelopePreset {
    Envelope envelope;
    std::uint16_t fadeout = 0;
    std::uint8_t vibratoType = 0, vibratoSweep = 0, vibratoDepth = 0, vibratoRate = 0;
    bool custom = false;
};
struct EnvelopePresets {
    std::array<EnvelopePreset, EnvelopePresetCount> volume, panning;
};

// Original app shapes, not a claim to reproduce FT2's factory preset values.
EnvelopePresets defaultEnvelopePresets();
const char* envelopePresetName(bool pan, std::size_t slot);

// Capture marks a slot custom. Panning captures only its envelope; volume
// also captures fadeout and instrument auto-vibrato, following FT2 semantics.
EnvelopePreset captureEnvelopePreset(const Instrument& instrument, bool pan);

// Validate before assigning. Panning changes only the panning envelope;
// volume also changes fadeout and auto-vibrato. Neither path changes samples,
// the key map, instrument name/extension or the opposite envelope.
void applyEnvelopePreset(Instrument& instrument, bool pan, const EnvelopePreset& preset);

// Fixed-size, versioned and checksummed little-endian application data.
// Malformed/truncated/unsupported files and invalid preset values throw.
// Decoding is all-or-nothing and never changes an existing bank.
std::vector<std::uint8_t> encodeEnvelopePresets(const EnvelopePresets& presets);
EnvelopePresets decodeEnvelopePresets(const std::vector<std::uint8_t>& bytes);

} // namespace brick::edit
