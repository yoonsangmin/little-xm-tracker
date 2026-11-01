#pragma once

#include "model/Document.hpp"

namespace brick::edit {

enum class CleanupScope { Patterns, Instruments, Both };

struct CleanupPlan {
    // Pattern IDs are zero-based; instrument IDs are one-based. Unused IDs
    // map to -1. IDs outside the selected scope are identity mappings, including
    // omitted slots. Instrument 0 remains 0 (keep the channel's previous choice).
    std::array<int, 256> patternMap{};
    std::array<int, 129> instrumentMap{};
    int patternsRemoved = 0, instrumentsRemoved = 0;
    int patternsRenumbered = 0, instrumentsRenumbered = 0;
    bool changed() const noexcept {
        return patternsRemoved || instrumentsRemoved || patternsRenumbered || instrumentsRenumbered;
    }
};

// Pattern cleanup keeps every pattern referenced by any order, including empty
// patterns. Instrument cleanup checks every retained pattern: with Instruments
// alone that includes unarranged patterns. Every nonzero instrument field counts,
// even without a note. No samples, channels, or duplicate patterns are merged.
// Retained IDs keep their original relative order. Omitted trailing patterns
// stay omitted and retain their implicit 64-row meaning.
// When cleaning instruments, a retained pattern referencing a missing instrument
// rejects cleanup. Pattern-only cleanup leaves instrument references untouched.
CleanupPlan analyzeCleanup(const Document& document, CleanupScope scope = CleanupScope::Both);

// Apply the same analysis and return its old-to-new maps for editor cursors.
// Validation/allocation failures leave the original document untouched.
CleanupPlan cleanupUnused(Document& document, CleanupScope scope = CleanupScope::Both);

} // namespace brick::edit
