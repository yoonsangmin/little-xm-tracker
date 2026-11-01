#include "model/Minimise.hpp"
#include "io/Codec.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int checks = 0;
void check(bool condition, const char* message) { ++checks; if (!condition) throw std::runtime_error(message); }
template<class Fn> void reject(Fn fn, const char* message) {
    bool failed = false;
    try { fn(); } catch (const std::exception&) { failed = true; }
    check(failed, message);
}
bool sameEnvelope(const brick::Envelope& a, const brick::Envelope& b) {
    if (a.count != b.count || a.sustain != b.sustain || a.loopStart != b.loopStart || a.loopEnd != b.loopEnd || a.flags != b.flags) return false;
    for (std::size_t n = 0; n < a.points.size(); ++n)
        if (a.points[n].x != b.points[n].x || a.points[n].y != b.points[n].y) return false;
    return true;
}
bool sameSample(const brick::Sample& a, const brick::Sample& b) {
    return a.name == b.name && a.pcm == b.pcm && a.sixteenBit == b.sixteenBit && a.volume == b.volume &&
        a.panning == b.panning && a.loopType == b.loopType && a.finetune == b.finetune && a.relativeNote == b.relativeNote &&
        a.loopStart == b.loopStart && a.loopLength == b.loopLength && a.reserved == b.reserved;
}
bool sameInstrument(const brick::Instrument& a, const brick::Instrument& b) {
    if (a.name != b.name || a.noteToSample != b.noteToSample || !sameEnvelope(a.volumeEnvelope, b.volumeEnvelope) ||
        !sameEnvelope(a.panningEnvelope, b.panningEnvelope) || a.vibratoType != b.vibratoType || a.vibratoSweep != b.vibratoSweep ||
        a.vibratoDepth != b.vibratoDepth || a.vibratoRate != b.vibratoRate || a.fadeout != b.fadeout || a.extension != b.extension ||
        a.samples.size() != b.samples.size()) return false;
    for (std::size_t n = 0; n < a.samples.size(); ++n) if (!sameSample(a.samples[n], b.samples[n])) return false;
    return true;
}
bool sameDocument(const brick::Document& a, const brick::Document& b) {
    if (a.name != b.name || a.channels != b.channels || a.restart != b.restart || a.speed != b.speed || a.bpm != b.bpm ||
        a.linear != b.linear || a.orders != b.orders || a.patterns.size() != b.patterns.size() || a.instruments.size() != b.instruments.size()) return false;
    for (std::size_t n = 0; n < a.patterns.size(); ++n) {
        const auto& x = a.patterns[n]; const auto& y = b.patterns[n];
        if (x.rows != y.rows || x.cells.size() != y.cells.size()) return false;
        for (std::size_t i = 0; i < x.cells.size(); ++i) {
            const auto& c = x.cells[i]; const auto& d = y.cells[i];
            if (c.note != d.note || c.instrument != d.instrument || c.volume != d.volume || c.effect != d.effect || c.param != d.param) return false;
        }
    }
    for (std::size_t n = 0; n < a.instruments.size(); ++n) if (!sameInstrument(a.instruments[n], b.instruments[n])) return false;
    return true;
}
bool sameSummary(const brick::edit::MinimiseSummary& a, const brick::edit::MinimiseSummary& b) {
    return a.samplesChanged == b.samplesChanged && a.framesRemoved == b.framesRemoved && a.bytesRemoved == b.bytesRemoved && a.changed() == b.changed();
}
brick::Sample sampleFixture(bool sixteenBit, std::uint8_t loopType, std::size_t frames, std::uint32_t first, std::uint32_t length) {
    brick::Sample sample; sample.name = "KEEP ATTACK AND LOOP";
    sample.sixteenBit = sixteenBit; sample.volume = 37; sample.panning = 212;
    sample.relativeNote = -13; sample.finetune = 17; sample.reserved = 0x4d;
    sample.loopType = loopType; sample.loopStart = first; sample.loopLength = length;
    for (std::size_t n = 0; n < frames; ++n) {
        const int magnitude = sixteenBit ? 1001 + int(n) * 79 : (int(n) + 1) * 256;
        sample.pcm.push_back(static_cast<std::int16_t>(n % 2 ? -magnitude : magnitude));
    }
    return sample;
}
std::vector<std::uint8_t> sampleXi(const brick::Sample& sample) {
    brick::Instrument instrument; instrument.samples.push_back(sample);
    return brick::io::encodeXi(instrument);
}
brick::Instrument instrumentFixture() {
    brick::Instrument instrument; instrument.name = "KEEP MULTISAMPLE";
    instrument.samples.push_back(sampleFixture(true, 1, 10, 2, 3)); // 5 frames / 10 bytes.
    instrument.samples.push_back(sampleFixture(false, 2, 8, 1, 4)); // 3 frames / 3 bytes.
    instrument.samples.push_back(sampleFixture(false, 0, 6, std::numeric_limits<std::uint32_t>::max(), std::numeric_limits<std::uint32_t>::max()));
    instrument.samples.push_back(sampleFixture(true, 3, 6, 2, 4)); // Already ends at the final frame.
    instrument.samples.emplace_back(); instrument.samples.back().name = "KEEP EMPTY SLOT";
    for (std::size_t n = 0; n < instrument.noteToSample.size(); ++n) instrument.noteToSample[n] = std::uint8_t(n % instrument.samples.size());
    auto& volume = instrument.volumeEnvelope;
    volume.count = 3; volume.points[0] = {0,64}; volume.points[1] = {12,48}; volume.points[2] = {30,0};
    volume.flags = 7; volume.sustain = 1; volume.loopEnd = 2;
    auto& pan = instrument.panningEnvelope;
    pan.count = 2; pan.points[0] = {0,24}; pan.points[1] = {20,40}; pan.flags = 1;
    instrument.vibratoType = 2; instrument.vibratoSweep = 87; instrument.vibratoDepth = 9; instrument.vibratoRate = 31;
    instrument.fadeout = 1337; instrument.extension[0] = 1; instrument.extension[21] = 42;
    return instrument;
}
brick::Document documentFixture() {
    brick::Document document; document.name = "KEEP SONG AND IDS";
    document.channels = 4; document.speed = 3; document.bpm = 142; document.restart = 2; document.linear = false;
    document.orders = {1,0,1}; document.patterns.resize(2);
    document.patterns[0].rows = 3; document.patterns[0].cells.resize(12);
    document.patterns[1].rows = 2; document.patterns[1].cells.resize(8);
    document.patterns[0].at(0,0,4) = {49,1,0x30,4,0x47};
    document.patterns[0].at(1,2,4) = {0,3,0,0,0};
    document.patterns[1].at(0,1,4) = {61,2,0x40,0x0f,6};
    document.instruments.push_back(instrumentFixture());
    brick::Instrument other; other.name = "SECOND INSTRUMENT";
    other.samples.push_back(sampleFixture(false, 3, 12, 2, 3)); // 7 frames / 7 bytes.
    document.instruments.push_back(other);
    document.instruments.emplace_back(); document.instruments.back().name = "KEEP EMPTY INSTR";
    return document;
}

void sampleLoopsAndNoops() {
    using namespace brick;
    check(!edit::MinimiseSummary{}.changed(), "A default summary reports no work");
    constexpr std::uint8_t loopTypes[]{1,2,3};
    for (const auto type : loopTypes) {
        for (bool sixteenBit : {false,true}) {
            auto sample = sampleFixture(sixteenBit, type, 12, 3, 4);
            sample.pcm.reserve(4096); const auto capacityBefore = sample.pcm.capacity();
            const auto before = sample; const auto xiBefore = sampleXi(sample);
            const auto plan = edit::analyzeMinimise(sample);
            check(plan.changed() && plan.samplesChanged == 1 && plan.framesRemoved == 5 && plan.bytesRemoved == (sixteenBit ? 10u : 5u),
                  "Every enabled loop type counts only the suffix using its stored bit depth");
            check(sameSample(sample, before) && sampleXi(sample) == xiBefore, "Sample analysis preserves exact PCM, metadata and XI bytes");
            const auto applied = edit::minimise(sample);
            auto expected = before; expected.pcm.resize(7);
            check(sameSummary(applied, plan) && sameSample(sample, expected), "Minimise keeps the complete attack and loop prefix plus every metadata field");
            check(sample.pcm.front() == before.pcm.front() && sample.pcm[2] == before.pcm[2] && before.pcm.back() != 0,
                  "The pre-loop attack remains intact and nonzero trailing PCM is removed without a silence heuristic");
            check(sample.pcm.capacity() < capacityBefore, "Successful minimisation releases the old generously reserved PCM allocation");
            const auto xiAfter = sampleXi(sample);
            check(xiBefore.size() - xiAfter.size() == applied.bytesRemoved, "Sample XI file reduction matches the reported saved PCM bytes");
            check(io::encodeXi(io::decodeXi(xiAfter)) == xiAfter, "All enabled loop types retain valid metadata through XI round trip");
            const auto noMore = edit::minimise(sample);
            check(!noMore.changed() && noMore.framesRemoved == 0 && noMore.bytesRemoved == 0 && sampleXi(sample) == xiAfter,
                  "Minimise is idempotent once PCM ends at the loop end");
        }
    }
    auto disabled = sampleFixture(false, 0, 8, std::numeric_limits<std::uint32_t>::max(), std::numeric_limits<std::uint32_t>::max());
    const auto disabledBefore = disabled; const auto disabledXi = sampleXi(disabled);
    check(!edit::analyzeMinimise(disabled).changed() && !edit::minimise(disabled).changed() && sameSample(disabled, disabledBefore) && sampleXi(disabled) == disabledXi,
          "Disabled loops ignore even maximal stale markers and retain all PCM");
    Sample empty; empty.name = "EMPTY"; empty.loopStart = std::numeric_limits<std::uint32_t>::max(); empty.loopLength = std::numeric_limits<std::uint32_t>::max();
    const auto emptyBefore = empty;
    check(!edit::analyzeMinimise(empty).changed() && !edit::minimise(empty).changed() && sameSample(empty, emptyBefore),
          "An empty unlooped sample is unchanged regardless of stored loop markers");
    Instrument noSamples; noSamples.name = "EMPTY INSTRUMENT";
    check(!edit::analyzeMinimise(noSamples).changed() && !edit::minimise(noSamples).changed() && noSamples.name == "EMPTY INSTRUMENT",
          "An instrument without samples is a no-op");
    Document noInstruments;
    check(!edit::analyzeMinimise(noInstruments).changed() && !edit::minimise(noInstruments).changed(), "A document without instruments is a no-op");
}

void invalidLoopValidation() {
    using namespace brick;
    std::vector<Sample> invalid;
    invalid.push_back(sampleFixture(true, 4, 10, 0, 0));
    invalid.push_back(sampleFixture(true, 1, 10, 0, 0));
    invalid.push_back(sampleFixture(true, 2, 10, 10, 1));
    invalid.push_back(sampleFixture(true, 3, 10, 8, 3));
    invalid.push_back(sampleFixture(true, 1, 10, 1, std::numeric_limits<std::uint32_t>::max()));
    invalid.push_back(sampleFixture(false, 2, 10, 2, std::numeric_limits<std::uint32_t>::max() - 1));
    invalid.push_back(sampleFixture(false, 3, 0, 0, 1));
    invalid.push_back(sampleFixture(false, 255, 0, 0, 0));
    for (auto& sample : invalid) {
        const auto before = sample;
        reject([&]{edit::analyzeMinimise(sample);}, "Analysis rejects malformed loop modes, empty ranges, bounds and uint32 end overflow");
        check(sameSample(sample, before), "Rejected analysis leaves even malformed sample metadata unchanged");
        reject([&]{edit::minimise(sample);}, "Minimise rejects malformed loops before replacing any PCM");
        check(sameSample(sample, before), "Rejected sample minimisation leaves PCM and metadata intact");
    }
}

void instrumentAndDocumentBatches() {
    using namespace brick;
    auto instrument = instrumentFixture(); const auto before = instrument; const auto xiBefore = io::encodeXi(instrument);
    const auto plan = edit::analyzeMinimise(instrument);
    check(plan.samplesChanged == 2 && plan.framesRemoved == 8 && plan.bytesRemoved == 13 && plan.changed(),
          "Instrument summaries add mixed eight-bit and sixteen-bit savings while skipping unchanged slots");
    check(sameInstrument(instrument, before) && io::encodeXi(instrument) == xiBefore, "Instrument analysis leaves all slots and instrument metadata untouched");
    const auto applied = edit::minimise(instrument);
    auto expected = before; expected.samples[0].pcm.resize(5); expected.samples[1].pcm.resize(5);
    check(sameSummary(applied, plan) && sameInstrument(instrument, expected), "Instrument minimisation preserves sample IDs, names, key map, envelopes and all unrelated PCM");
    const auto xiAfter = io::encodeXi(instrument);
    check(xiBefore.size() - xiAfter.size() == applied.bytesRemoved && io::encodeXi(io::decodeXi(xiAfter)) == xiAfter,
          "Mixed-depth instrument savings equal the actual XI reduction and the result round trips");

    auto document = documentFixture(); const auto docBefore = document; const auto xmBefore = io::encodeXm(document);
    const auto documentPlan = edit::analyzeMinimise(document);
    check(documentPlan.samplesChanged == 3 && documentPlan.framesRemoved == 15 && documentPlan.bytesRemoved == 20,
          "Document summaries accumulate savings across every instrument");
    check(sameDocument(document, docBefore) && io::encodeXm(document) == xmBefore, "Document analysis never changes song data or its exact XM encoding");
    const auto documentApplied = edit::minimise(document);
    auto docExpected = docBefore;
    docExpected.instruments[0].samples[0].pcm.resize(5); docExpected.instruments[0].samples[1].pcm.resize(5);
    docExpected.instruments[1].samples[0].pcm.resize(5);
    check(sameSummary(documentApplied, documentPlan) && sameDocument(document, docExpected),
          "Document minimisation changes only planned suffixes and preserves patterns, orders, instrument IDs, mappings and metadata");
    const auto xmAfter = io::encodeXm(document);
    check(xmBefore.size() - xmAfter.size() == documentApplied.bytesRemoved, "Reported document bytes equal the actual XM file-size reduction");
    check(io::encodeXm(io::decodeXm(xmAfter)) == xmAfter, "Minimised documents retain exact XM data through round trip");
    check(!edit::analyzeMinimise(document).changed() && !edit::minimise(document).changed() && io::encodeXm(document) == xmAfter,
          "Repeated document analysis and minimisation become no-ops");
}

void transactionalBatches() {
    using namespace brick;
    auto instrument = instrumentFixture();
    instrument.samples.push_back(sampleFixture(true, 1, 6, 0, 0));
    const auto before = instrument; const auto* firstPcm = instrument.samples[0].pcm.data();
    reject([&]{edit::analyzeMinimise(instrument);}, "Instrument analysis validates a malformed last sample after earlier eligible samples");
    check(sameInstrument(instrument, before), "Failed batch analysis does not alter earlier valid samples");
    reject([&]{edit::minimise(instrument);}, "A malformed last sample rejects the entire instrument batch");
    check(sameInstrument(instrument, before) && instrument.samples[0].pcm.data() == firstPcm,
          "Instrument failure commits no prefix replacement and preserves all original allocations and metadata");

    auto document = documentFixture();
    document.instruments.back().samples.push_back(sampleFixture(false, 2, 6, 1, std::numeric_limits<std::uint32_t>::max()));
    const auto docBefore = document; const auto* firstDocumentPcm = document.instruments[0].samples[0].pcm.data();
    reject([&]{edit::analyzeMinimise(document);}, "Document analysis validates a malformed sample in the final instrument");
    check(sameDocument(document, docBefore), "Failed document analysis retains every original instrument and song field");
    reject([&]{edit::minimise(document);}, "A late invalid loop rejects the complete multi-instrument operation");
    check(sameDocument(document, docBefore) && document.instruments[0].samples[0].pcm.data() == firstDocumentPcm,
          "Document failure leaves earlier eligible samples and all unrelated data unchanged");
}
}

int main() {
    try {
        sampleLoopsAndNoops(); invalidLoopValidation(); instrumentAndDocumentBatches(); transactionalBatches();
        std::cout << "Minimise passed: " << checks << " checks\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
