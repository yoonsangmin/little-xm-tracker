#pragma once

#include "model/Cleanup.hpp"

namespace brick::edit {

struct TrimOptions {
    bool unusedPatterns = true, unusedInstruments = true, unusedSamples = true;
    bool unusedChannels = true, sampleTails = true, convertTo8Bit = false;
};

struct TrimResult {
    Document document;
    CleanupPlan ids;
    // Indexed by OLD instrument ID and OLD sample index, even when instruments
    // move. Removed slots map to -1; untouched and implicit slots keep identity.
    std::array<std::array<int, 16>, 129> sampleMap{};
    int samplesRemoved = 0, samplesRenumbered = 0, channelsRemoved = 0;
    int samplesTrimmed = 0, samplesConverted = 0;
    std::uint64_t beforeBytes = 0, afterBytes = 0;
    bool changed() const noexcept;
};

// Prepare an independent, fully validated document without changing the source.
// Sizes are exact encodeXm output sizes. Samples count as used when mapped by
// any of the 96 instrument keys, including mapped empty samples. Codec-valid
// empty instruments retain their implicit sample 0. Existing codec validation
// rejects other missing sample references before any cleanup is considered.
// Channels shrink only at the right edge, to an even count (minimum two), never
// expanding an existing one/odd-channel song. All-empty songs keep their count.
// Optional 8-bit conversion discards the low byte as FT2 does, without rounding.
TrimResult prepareTrim(const Document& document, const TrimOptions& options = {});

} // namespace brick::edit
