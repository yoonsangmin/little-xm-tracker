#pragma once

#include "model/EditOperations.hpp"

namespace brick::edit {

struct PasteMask {
    bool note = true, instrument = true, volume = true, effect = true, param = true;
    bool skipEmpty = false;
};

// Empty FX means the pair 000. Thus arpeggio 037 copies its zero command and
// effect-memory command 400 copies its zero parameter even with skipEmpty on.
Cell mergeCell(const Cell& destination, const Cell& source, const PasteMask& mask);
void pasteMaskedBlock(Pattern& pattern, std::size_t channels, std::size_t row,
                      std::size_t channel, const PatternBlock& block, const PasteMask& mask);

// Expand inserts a blank row after every source row; length is capped at 256.
// Shrink keeps even rows and rounds an odd source length up. Nonempty rows that
// would be dropped require allowDiscard. Raw Bxx/Dxx and all other bytes stay
// unchanged: these operations do not retime effects or alter order references.
void expandPattern(Pattern& pattern, std::size_t channels, bool allowDiscard = false);
void shrinkPattern(Pattern& pattern, std::size_t channels, bool allowDiscard = false);

// Clear contents without removing or renumbering slots. Valid missing slots are
// no-ops. Patterns retain their length; instruments reset all their own settings
// and samples; sample clears preserve the owner's key map and sample slot count.
void clearPatternSlot(Document& document, int patternZeroBased);
void clearInstrumentSlot(Document& document, int instrumentOneBased);
void clearSampleSlot(Document& document, int instrumentOneBased, int sampleZeroBased);

// Replace the entire numbered pattern, including row length and blank cells.
// Missing slots are created with empty gaps; Order references remain unchanged.
// Clipboard channel count must match the document; no implicit channel loss.
void replacePattern(Document& document, int destinationZeroBased,
                    const Pattern& snapshot, int sourceChannels);

// Instrument numbers are one-based; sample indices are zero-based. Destination
// slots must exist or be exactly the next slot (max 128 instruments/16 samples).
// Copies own independent PCM and preserve every metadata field. Swaps exchange
// slot contents, without rewriting pattern instrument numbers or key maps.
void replaceInstrument(Document& document, int destinationOneBased, const Instrument& snapshot);
void copyInstrument(Document& document, int sourceOneBased, int destinationOneBased);
void swapInstruments(Document& document, int firstOneBased, int secondOneBased);
void replaceSample(Document& document, int destinationInstrumentOneBased,
                   int destinationSampleZeroBased, const Sample& snapshot);
void copySample(Document& document, int sourceInstrumentOneBased, int sourceSampleZeroBased,
                int destinationInstrumentOneBased, int destinationSampleZeroBased);
void swapSamples(Document& document, int firstInstrumentOneBased, int firstSampleZeroBased,
                 int secondInstrumentOneBased, int secondSampleZeroBased);

} // namespace brick::edit
