#pragma once

#include "model/EditOperations.hpp"

namespace brick::edit {

struct ChopOptions {
    int count = 8;
    bool snapToTransients = false;
    int firstNote = 49; // XM note: 1=C-0, 49=C-4.
};
struct ChopResult {
    // Absolute source-frame positions, including both selection endpoints.
    std::vector<std::size_t> boundaries;
    std::vector<Sample> slices;
    int firstNote = 49;
    int snappedBoundaries = 0;
};

// Split a nonempty half-open range into 1..16 nonempty, contiguous slices.
// Equal division is exact by default. Optional onset snapping stays within
// one quarter of a cell and 25 ms at the source's C-4 rate; absent a clear
// energy rise, the equal boundary is retained. PCM is never resampled.
// Loops are cleared. Depth, volume, panning and finetune are retained; each
// relative note compensates its mapped trigger note to keep the C-4 speed.
// Invalid ranges/counts/trigger notes or tuning overflow throw, without mutation.
ChopResult chopSample(const Sample& source, FrameRange range, const ChopOptions& options = {});

// Apply edited absolute boundaries without changing or snapping them. They
// must span range exactly and define 1..16 strictly increasing nonempty slices.
// Uses the same metadata and trigger-note tuning as chopSample. Build only on
// Apply; a UI draft can move/add/remove boundaries without copying any PCM.
ChopResult chopSampleAtBoundaries(const Sample& source, FrameRange range,
                                 const std::vector<std::size_t>& boundaries, int firstNote = 49);

struct ChopPlacement {
    Instrument instrument;
    std::vector<int> slots; // Zero-based destination sample slots.
    bool replaced = false;
};

// An existing slot is vacant when its PCM is empty; omitted slots count too.
int emptySampleSlots(const Instrument& instrument);

// Fill vacant slots in ascending order, preserving all other instrument data
// except the consecutive trigger-note mappings. Insufficient space throws.
// replaceAll explicitly starts with a fresh default Instrument. Passing an
// empty Instrument without replaceAll is the new-instrument placement path.
// Both inputs remain unchanged, including on failure.
ChopPlacement placeChop(const Instrument& instrument, const ChopResult& chop, bool replaceAll = false);

} // namespace brick::edit
