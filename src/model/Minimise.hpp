#pragma once

#include "model/Document.hpp"

namespace brick::edit {

struct MinimiseSummary {
    std::size_t samplesChanged = 0;
    std::uint64_t framesRemoved = 0, bytesRemoved = 0;
    bool changed() const noexcept { return samplesChanged != 0; }
};

// Remove only the PCM suffix after the exclusive end of an enabled XM loop.
// Loop types 1, 2 and 3 are supported. Disabled loops ignore stored markers;
// empty unlooped samples and loops already ending at the PCM end are no-ops.
// Enabled loops require a nonempty, in-bounds range. Invalid loops/types throw.
// Summary bytes are saved XM PCM bytes (8/16-bit), not in-memory allocation.
MinimiseSummary analyzeMinimise(const Sample& sample);
MinimiseSummary analyzeMinimise(const Instrument& instrument);
MinimiseSummary analyzeMinimise(const Document& document);

// The entire batch is validated before changed prefixes are allocated. All
// prefixes are prepared before noexcept swaps commit any change, so failures
// leave every input sample intact. Metadata, maps, IDs and envelopes stay put.
// Old PCM allocations are released on success; no whole-document copies occur.
MinimiseSummary minimise(Sample& sample);
MinimiseSummary minimise(Instrument& instrument);
MinimiseSummary minimise(Document& document);

} // namespace brick::edit
