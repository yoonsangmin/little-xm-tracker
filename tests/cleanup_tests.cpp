#include "model/Cleanup.hpp"
#include "model/EditOperations.hpp"
#include "io/Codec.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
int checks = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
bool same(const brick::Cell& a, const brick::Cell& b) {
    return a.note == b.note && a.instrument == b.instrument && a.volume == b.volume &&
           a.effect == b.effect && a.param == b.param;
}
brick::Pattern pattern(int rows, int channels) {
    brick::Pattern result;
    result.rows = static_cast<std::uint16_t>(rows);
    result.cells.resize(std::size_t(rows) * std::size_t(channels));
    return result;
}
brick::Document fixture() {
    auto document = brick::makeDemoDocument();
    document.channels = 4;
    document.name = "Cleanup fixture";
    document.orders = {2, 0, 2};
    document.restart = 2;
    document.patterns = {pattern(17, 4), pattern(8, 4), pattern(32, 4)};
    document.patterns[0].at(0, 0, 4) = {49, 1, 0x30, 0x0B, 2};
    document.patterns[0].at(1, 2, 4) = {0, 4, 0xF3, 0x0E, 0x60}; // Instrument-only row.
    document.patterns[0].at(2, 1, 4) = {97, 0, 0x55, 0x0F, 6};
    document.patterns[1].at(0, 0, 4) = {60, 2, 0x44, 4, 0x47};
    document.patterns[2].at(0, 1, 4) = {65, 3, 0xB7, 4, 0x47};
    const auto source = document.instruments[0];
    document.instruments = {source, source, source, {}, {}};
    document.instruments[0].name = "Keep original";
    document.instruments[1].name = "Unused pattern only";
    auto& multi = document.instruments[2];
    multi.name = "Keep multisample";
    multi.samples.push_back(multi.samples[0]);
    multi.samples[1].name = "Unused sample kept";
    multi.samples[1].sixteenBit = false;
    multi.samples[1].relativeNote = 12;
    multi.samples[1].finetune = -17;
    multi.samples[1].loopType = 2;
    // Its second sample is intentionally not mapped: sample cleanup is not requested.
    multi.fadeout = 1536;
    multi.panningEnvelope.count = 2;
    multi.panningEnvelope.points[0] = {0, 32};
    multi.panningEnvelope.points[1] = {15, 64};
    multi.panningEnvelope.flags = 1;
    document.instruments[3].name = "Referenced empty";
    document.instruments[4].name = "Unused empty";
    return document;
}

void retainedReferencesAndMetadata() {
    auto document = fixture();
    const auto before = document;
    const auto encodedBefore = brick::io::encodeXm(document);
    const auto plan = brick::edit::analyzeCleanup(document);
    check(brick::io::encodeXm(document) == encodedBefore, "Analysis must not change the document");
    check(plan.changed() && plan.patternsRemoved == 1 && plan.instrumentsRemoved == 2,
          "Cleanup counts stored unused patterns and instruments");
    check(plan.patternMap[0] == 0 && plan.patternMap[1] == -1 && plan.patternMap[2] == 1,
          "Pattern maps retain original relative order");
    check(plan.instrumentMap[0] == 0 && plan.instrumentMap[1] == 1 && plan.instrumentMap[2] == -1 &&
          plan.instrumentMap[3] == 2 && plan.instrumentMap[4] == 3 && plan.instrumentMap[5] == -1,
          "Instrument maps retain instrument-only and empty references");
    const auto applied = brick::edit::cleanupUnused(document);
    check(applied.patternMap == plan.patternMap && applied.instrumentMap == plan.instrumentMap,
          "Applied result matches the preview maps");
    check(document.orders == std::vector<std::uint8_t>({1, 0, 1}) && document.restart == 2,
          "Repeated order references and restart order identity are preserved");
    check(document.name == before.name && document.channels == before.channels &&
          document.bpm == before.bpm && document.speed == before.speed && document.linear == before.linear,
          "Cleanup leaves project settings unchanged");
    check(document.patterns.size() == 2 && document.patterns[0].rows == 17 && document.patterns[1].rows == 32,
          "Pattern sizes are preserved and no patterns are merged");
    for (std::size_t oldId = 0; oldId < before.patterns.size(); ++oldId) {
        const auto newId = plan.patternMap[oldId];
        if (newId < 0) continue;
        for (std::size_t n = 0; n < before.patterns[oldId].cells.size(); ++n) {
            auto expected = before.patterns[oldId].cells[n];
            expected.instrument = static_cast<std::uint8_t>(plan.instrumentMap[expected.instrument]);
            check(same(document.patterns[std::size_t(newId)].cells[n], expected),
                  "Only instrument references change; notes, volume and both FX bytes remain exact");
        }
    }
    check(document.instruments.size() == 3 && document.instruments[2].samples.empty(),
          "Explicitly referenced empty instruments survive");
    check(brick::io::encodeXi(document.instruments[1]) == brick::io::encodeXi(before.instruments[2]),
          "Retained instrument preserves all samples, loops, tuning, keymap and envelopes");
    const auto cleaned = brick::io::encodeXm(document);
    const auto reloaded = brick::io::decodeXm(cleaned);
    check(brick::io::encodeXm(reloaded) == cleaned, "Cleaned XM roundtrip preserves the resulting arrangement");
    const auto delta = brick::edit::DocumentDelta::between(before, document);
    delta.undo(document);
    check(brick::io::encodeXm(document) == encodedBefore, "Undo restores deleted content and all original IDs");
    delta.redo(document);
    check(brick::io::encodeXm(document) == cleaned, "Redo restores the complete cleanup transaction");
    check(!brick::edit::cleanupUnused(document).changed() && brick::io::encodeXm(document) == cleaned,
          "Cleanup is idempotent");
}

void emptyAndOmittedPatterns() {
    brick::Document document;
    document.channels = 1;
    document.orders = {7, 255, 7};
    document.restart = 1;
    const auto result = brick::edit::cleanupUnused(document);
    check(result.changed() && result.patternsRemoved == 0 && result.patternsRenumbered == 2,
          "Sparse virtual pattern IDs can be compacted without deleting stored data");
    check(document.patterns.empty() && document.orders == std::vector<std::uint8_t>({0, 1, 0}) && document.restart == 1,
          "Omitted 64-row patterns stay omitted after renumbering");
    check(document.instruments.empty(), "A song without instruments remains valid");
    check(!brick::edit::analyzeCleanup(document).changed(), "Already compact virtual patterns need no second cleanup");
    check(brick::io::decodeXm(brick::io::encodeXm(document)).orders == document.orders,
          "Omitted references survive XM save and reopen");

    document.patterns = {pattern(7, 1), pattern(29, 1), pattern(13, 1)};
    document.orders = {1, 9, 1};
    const auto mixed = brick::edit::cleanupUnused(document);
    check(mixed.patternsRemoved == 2 && document.patterns.size() == 1 && document.patterns[0].rows == 29,
          "An explicitly referenced empty pattern retains its non-default length");
    check(document.orders == std::vector<std::uint8_t>({0, 1, 0}), "Stored and virtual patterns compact consistently");

    auto allUnused = fixture();
    allUnused.orders = {255}; allUnused.restart = 0;
    const auto all = brick::edit::cleanupUnused(allUnused);
    check(all.patternsRemoved == 3 && all.instrumentsRemoved == 5 && allUnused.patterns.empty() && allUnused.instruments.empty(),
          "Only an omitted empty pattern remains when every stored pattern and instrument is unused");
}

void separateScopes() {
    using brick::edit::CleanupScope;
    auto document = fixture();
    const auto original = document;
    const auto originalBytes = brick::io::encodeXm(original);
    const auto patterns = brick::edit::cleanupUnused(document, CleanupScope::Patterns);
    check(patterns.patternsRemoved == 1 && patterns.instrumentsRemoved == 0 &&
          patterns.instrumentsRenumbered == 0 && patterns.instrumentMap[128] == 128,
          "Pattern-only cleanup leaves every instrument ID untouched, including omitted slots");
    check(document.orders == std::vector<std::uint8_t>({1, 0, 1}) && document.instruments.size() == 5,
          "Pattern-only cleanup compacts orders without deleting any instruments");
    for (std::size_t id = 0; id < original.instruments.size(); ++id)
        check(brick::io::encodeXi(document.instruments[id]) == brick::io::encodeXi(original.instruments[id]),
              "Pattern-only cleanup preserves every sample and instrument exactly");
    for (std::size_t id = 0; id < original.patterns.size(); ++id) {
        if (patterns.patternMap[id] < 0) continue;
        const auto& retained = document.patterns[std::size_t(patterns.patternMap[id])];
        for (std::size_t n = 0; n < retained.cells.size(); ++n)
            check(same(retained.cells[n], original.patterns[id].cells[n]),
                  "Pattern-only cleanup preserves all cell bytes, including instrument fields");
    }
    check(!brick::edit::analyzeCleanup(document, CleanupScope::Patterns).changed(),
          "Pattern-only cleanup is idempotent even when unused instruments remain");
    const auto patternsBytes = brick::io::encodeXm(document);
    const auto patternDelta = brick::edit::DocumentDelta::between(original, document);
    patternDelta.undo(document);
    check(brick::io::encodeXm(document) == originalBytes, "Undo restores pattern-only cleanup in one transaction");
    patternDelta.redo(document);
    check(brick::io::encodeXm(document) == patternsBytes, "Redo reapplies pattern-only cleanup without touching instruments");
    check(brick::io::encodeXm(brick::io::decodeXm(patternsBytes)) == patternsBytes,
          "Pattern-only cleanup survives XM save and reopen");

    document = fixture();
    document.patterns[1].at(0, 0, document.channels).instrument = 3; // Unarranged but retained.
    document.patterns[2].at(0, 1, document.channels).instrument = 4;
    const auto beforeInstruments = document;
    const auto instruments = brick::edit::cleanupUnused(document, CleanupScope::Instruments);
    check(instruments.patternsRemoved == 0 && instruments.patternsRenumbered == 0 &&
          instruments.patternMap[255] == 255 && instruments.instrumentsRemoved == 2,
          "Instrument-only cleanup keeps every stored and omitted pattern ID");
    check(document.orders == beforeInstruments.orders && document.restart == beforeInstruments.restart &&
          document.patterns.size() == beforeInstruments.patterns.size(),
          "Instrument-only cleanup preserves the complete arrangement and unarranged patterns");
    check(instruments.instrumentMap[3] == 2 && document.patterns[1].at(0, 0, document.channels).instrument == 2,
          "An instrument referenced only by an unarranged pattern is retained and its cells are remapped");
    check(document.instruments.size() == 3 && document.instruments[2].samples.empty(),
          "Instrument-only cleanup keeps referenced empty instruments and instrument-only cells");
    for (std::size_t id = 0; id < beforeInstruments.patterns.size(); ++id) {
        check(document.patterns[id].rows == beforeInstruments.patterns[id].rows,
              "Instrument-only cleanup preserves every pattern length");
        for (std::size_t n = 0; n < document.patterns[id].cells.size(); ++n) {
            auto expected = beforeInstruments.patterns[id].cells[n];
            expected.instrument = static_cast<std::uint8_t>(instruments.instrumentMap[expected.instrument]);
            check(same(document.patterns[id].cells[n], expected),
                  "Instrument-only cleanup changes only instrument references in every retained pattern");
        }
    }
    check(brick::io::encodeXi(document.instruments[1]) == brick::io::encodeXi(beforeInstruments.instruments[2]),
          "Instrument-only cleanup preserves complete multisample instruments");
    const auto instrumentBytes = brick::io::encodeXm(document);
    const auto instrumentDelta = brick::edit::DocumentDelta::between(beforeInstruments, document);
    instrumentDelta.undo(document);
    check(brick::io::encodeXm(document) == brick::io::encodeXm(beforeInstruments),
          "Undo restores instrument-only cleanup and unarranged references");
    instrumentDelta.redo(document);
    check(brick::io::encodeXm(document) == instrumentBytes &&
          !brick::edit::analyzeCleanup(document, CleanupScope::Instruments).changed(),
          "Instrument-only cleanup redo is exact and leaves no second edit");
    check(brick::io::encodeXm(brick::io::decodeXm(instrumentBytes)) == instrumentBytes,
          "Instrument-only cleanup survives XM save and reopen");

    document = fixture();
    document.patterns[1].at(0, 0, document.channels).instrument = 255;
    const auto invalidBytes = brick::io::encodeXm(document);
    bool rejected = false;
    try { brick::edit::cleanupUnused(document, CleanupScope::Instruments); }
    catch (const std::runtime_error&) { rejected = true; }
    check(rejected && brick::io::encodeXm(document) == invalidBytes,
          "Instrument-only cleanup rejects dangling references even in unarranged patterns, without mutation");
    brick::edit::cleanupUnused(document, CleanupScope::Patterns);
    check(document.patterns.size() == 2 && document.instruments.size() == 5,
          "Pattern-only cleanup can safely discard a dangling reference in a removed pattern");
    document = fixture();
    document.patterns[2].at(0, 0, document.channels).instrument = 255;
    brick::edit::cleanupUnused(document, CleanupScope::Patterns);
    check(document.patterns[1].at(0, 0, document.channels).instrument == 255,
          "Pattern-only cleanup leaves a retained dangling instrument byte unchanged");

    brick::Document virtualOnly;
    virtualOnly.orders = {7, 255, 7};
    virtualOnly.instruments.resize(2);
    const auto virtualInstruments = brick::edit::cleanupUnused(virtualOnly, CleanupScope::Instruments);
    check(virtualInstruments.instrumentsRemoved == 2 && virtualOnly.patterns.empty() &&
          virtualOnly.orders == std::vector<std::uint8_t>({7, 255, 7}),
          "Instrument-only cleanup never renumbers or materializes omitted patterns");
}

void rejectionAndBoundaries() {
    auto invalid = fixture();
    invalid.patterns[2].at(0, 0, invalid.channels).instrument = 128;
    const auto before = brick::io::encodeXm(invalid);
    bool failed = false;
    try { brick::edit::cleanupUnused(invalid); } catch (const std::runtime_error&) { failed = true; }
    check(failed && brick::io::encodeXm(invalid) == before,
          "Dangling instrument references in kept patterns reject cleanup without document mutation");
    invalid = fixture();
    invalid.patterns[1].at(0, 0, invalid.channels).instrument = 255;
    check(brick::edit::cleanupUnused(invalid).patternsRemoved == 1,
          "A missing instrument used only in a deleted unused pattern does not block cleanup");

    brick::Document maximum;
    maximum.channels = 32;
    maximum.restart = 255;
    maximum.instruments.resize(128);
    for (int i = 0; i < 256; ++i) {
        maximum.orders.push_back(static_cast<std::uint8_t>(i));
        auto p = pattern(1, maximum.channels);
        p.at(0, 0, maximum.channels).instrument = static_cast<std::uint8_t>(i % 128 + 1);
        maximum.patterns.push_back(std::move(p));
    }
    const auto full = brick::edit::cleanupUnused(maximum);
    check(!full.changed() && full.patternMap[255] == 255 && full.instrumentMap[128] == 128,
          "Maximum legal pattern, instrument, order and channel counts retain valid identity maps");
    const auto sizes = std::make_pair(maximum.patterns.size(), maximum.instruments.size());
    maximum.patterns[0].cells.pop_back();
    failed = false;
    try { brick::edit::cleanupUnused(maximum); } catch (const std::runtime_error&) { failed = true; }
    check(failed && maximum.patterns.size() == sizes.first && maximum.instruments.size() == sizes.second &&
          maximum.patterns[0].cells.size() == 31, "Invalid pattern storage rejects before any mutation");
}
} // namespace

int main() {
    try {
        retainedReferencesAndMetadata();
        emptyAndOmittedPatterns();
        separateScopes();
        rejectionAndBoundaries();
        std::cout << "Cleanup tests passed: " << checks << " checks; references, empty/omitted patterns, metadata, history and XM roundtrip.\n";
    } catch (const std::exception& error) {
        std::cerr << "Cleanup test failure: " << error.what() << '\n';
        return 1;
    }
}
