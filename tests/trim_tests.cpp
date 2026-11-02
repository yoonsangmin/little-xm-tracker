#include "model/Trim.hpp"
#include "model/EditOperations.hpp"
#include "io/Codec.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
int checks = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
template<class Function> void reject(Function function, const char* message) {
    bool threw = false;
    try { function(); } catch (const std::exception&) { threw = true; }
    check(threw, message);
}
brick::edit::TrimOptions options(int bits) {
    return {bool(bits & 1), bool(bits & 2), bool(bits & 4), bool(bits & 8), bool(bits & 16), bool(bits & 32)};
}
brick::Pattern pattern(int rows, int channels) {
    brick::Pattern result; result.rows = std::uint16_t(rows); result.cells.resize(std::size_t(rows * channels)); return result;
}
brick::Sample sample(bool wide = true, int loop = 0) {
    brick::Sample result; result.name = "PCM"; result.sixteenBit = wide;
    result.pcm = wide ? std::vector<std::int16_t>{-32768,-32767,-257,-256,-255,-1,0,1,255,256,257,32767}
                      : std::vector<std::int16_t>{-32768,-256,0,256,32512};
    result.loopType = std::uint8_t(loop); result.loopStart = 1; result.loopLength = 3;
    result.volume = 51; result.panning = 205; result.finetune = -19; result.relativeNote = 7; result.reserved = 0x43;
    return result;
}
brick::Document fixture() {
    brick::Document result; result.name = "TRIM FIXTURE"; result.channels = 8; result.orders = {2,0,2}; result.restart = 2;
    result.patterns = {pattern(3,8),pattern(4,8),pattern(2,8)};
    result.patterns[0].at(0,0,8) = {49,3,0xB7,4,0x47};
    result.patterns[0].at(1,0,8) = {0,0,0,0x0B,2};
    result.patterns[1].at(1,7,8) = {60,2,0xF2,0x0E,0x91};
    result.patterns[2].at(0,2,8) = {0,0,0,9,3}; // Effect-only channel is essential.
    result.instruments.resize(3);
    for (auto& instrument : result.instruments) {
        instrument.name = "MULTI";
        instrument.samples = {sample(), sample(true,2), {}};
        instrument.samples[2].name = "MAPPED EMPTY";
        instrument.noteToSample.fill(1); instrument.noteToSample[95] = 2;
        instrument.volumeEnvelope.count = 2; instrument.volumeEnvelope.flags = 1;
        instrument.volumeEnvelope.points[0] = {0,64}; instrument.volumeEnvelope.points[1] = {12,32};
        instrument.fadeout = 1024; instrument.vibratoRate = 19; instrument.extension[21] = 9;
    }
    return result;
}
bool sameCell(const brick::Cell& a, const brick::Cell& b) {
    return a.note == b.note && a.instrument == b.instrument && a.volume == b.volume && a.effect == b.effect && a.param == b.param;
}

void allCombinations() {
    using namespace brick;
    const auto source = fixture(); const auto encoded = io::encodeXm(source);
    for (int bits = 0; bits < 64; ++bits) {
        const auto opt = options(bits); const auto result = edit::prepareTrim(source,opt);
        const auto after = io::encodeXm(result.document);
        check(io::encodeXm(source) == encoded, "Calculate never mutates the source");
        check(result.beforeBytes == encoded.size() && result.afterBytes == after.size(), "Reported sizes equal actual XM writer sizes for all 64 combinations");
        check(io::encodeXm(io::decodeXm(after)) == after, "Every option combination produces an exact XM round trip");
        check(edit::DocumentDelta::between(source,result.document).empty() == !result.changed(), "Changed agrees with actual document delta for all combinations");
        check(result.document.patterns.size() == (opt.unusedPatterns ? 2u : 3u), "Only the pattern option removes unarranged patterns");
        check(result.document.channels == (opt.unusedChannels && opt.unusedPatterns ? 4 : 8), "Channel cleanup scans retained patterns and keeps the FX-only channel");
        check(result.document.instruments.size() == (opt.unusedInstruments ? (opt.unusedPatterns ? 1u : 2u) : 3u), "Instrument cleanup scans every retained pattern");
        check(result.document.restart == source.restart && result.document.orders[0] == (opt.unusedPatterns ? 1 : 2), "Restart identity and repeated orders survive pattern compaction");
        check(!opt.unusedSamples || result.samplesRemoved == int(result.document.instruments.size()), "Only unmapped samples in retained instruments count as removed samples");
        check(opt.unusedSamples || (!result.samplesRemoved && !result.samplesRenumbered), "Sample option off leaves all sample IDs intact");
        check(opt.sampleTails || !result.samplesTrimmed, "Tail option off leaves PCM tails intact");
        check(opt.convertTo8Bit || !result.samplesConverted, "Depth option off leaves all precision intact");
        for (std::size_t oldPattern = 0; oldPattern < source.patterns.size(); ++oldPattern) {
            const int id = result.ids.patternMap[oldPattern]; if (id < 0) continue;
            const auto& retained = result.document.patterns[std::size_t(id)];
            for (std::size_t row = 0; row < retained.rows; ++row)
                for (std::size_t channel = 0; channel < result.document.channels; ++channel) {
                    auto expected = source.patterns[oldPattern].at(row,channel,source.channels);
                    expected.instrument = std::uint8_t(result.ids.instrumentMap[expected.instrument]);
                    check(sameCell(retained.at(row,channel,result.document.channels),expected), "All remaining note, volume, 9xx and other effect bytes remain intact");
                }
        }
        for (std::size_t oldId = 1; oldId <= source.instruments.size(); ++oldId) {
            const int newId = result.ids.instrumentMap[oldId];
            if (newId < 0) {
                check(std::all_of(result.sampleMap[oldId].begin(),result.sampleMap[oldId].end(),[](int id){return id == -1;}), "Removed instrument sample mappings are unavailable");
                continue;
            }
            const auto& retained = result.document.instruments[std::size_t(newId-1)];
            auto expected = source.instruments[oldId-1];
            if (opt.unusedSamples) {
                expected.samples.erase(expected.samples.begin());
                for (auto& id : expected.noteToSample) --id;
                check(result.sampleMap[oldId][0] == -1 && result.sampleMap[oldId][1] == 0 && result.sampleMap[oldId][2] == 1,
                      "Sample maps use old instrument IDs despite instrument compaction");
            }
            if (opt.sampleTails) for (auto& s : expected.samples) if (s.loopType) s.pcm.resize(4);
            if (opt.convertTo8Bit) for (auto& s : expected.samples) if (s.sixteenBit && !s.pcm.empty()) {
                for (auto& value : s.pcm) {
                    const auto high = static_cast<std::uint16_t>(value) & 0xFF00u;
                    value = std::int16_t(high < 32768 ? int(high) : int(high)-65536);
                }
                s.sixteenBit = false;
            }
            check(io::encodeXi(retained) == io::encodeXi(expected), "All 96 key mappings, empty slots, envelopes, tuning and PCM follow exactly the selected options");
            check(result.sampleMap[oldId][15] == 15, "Unmaterialized sample slots keep identity");
        }
        auto restored = result.document; const auto delta = edit::DocumentDelta::between(source,result.document);
        delta.undo(restored); check(io::encodeXm(restored) == encoded, "Undo restores full song, sample IDs, channels and PCM");
        delta.redo(restored); check(io::encodeXm(restored) == after, "Redo restores the calculated result exactly");
        check(!edit::prepareTrim(result.document,opt).changed(), "Cleanup is idempotent for every option combination");
    }
}

void channelEdges() {
    using namespace brick;
    Document source; source.channels = 8; source.orders = {0}; source.patterns = {pattern(2,8)};
    check(edit::prepareTrim(source,options(8)).document.channels == 8, "An all-empty song keeps its channels");
    const Cell markers[] = {{49,0,0,0,0},{0,1,0,0,0},{0,0,0x10,0,0},{0,0,0,9,0},{0,0,0,0,1}};
    for (auto marker : markers) {
        auto doc = source; doc.patterns[0].at(0,4,8) = marker;
        const auto result = edit::prepareTrim(doc,options(8));
        check(result.document.channels == 6 && result.channelsRemoved == 2, "Every individual cell byte protects its channel and even rounding");
        check(sameCell(result.document.patterns[0].at(0,4,6),marker), "Interior empty channels and high-channel marker stay in place");
    }
    for (int channels : {1,3,5}) {
        auto doc = source; doc.channels = std::uint16_t(channels); doc.patterns = {pattern(2,channels)};
        doc.patterns[0].at(0,std::size_t(channels-1),channels).param = 1;
        check(!edit::prepareTrim(doc,options(8)).changed(), "One and odd channel songs never expand");
        doc.patterns[0].cells.assign(std::size_t(channels*2),{}); doc.patterns[0].cells[0].note = 49;
        check(edit::prepareTrim(doc,options(8)).document.channels == std::min(channels,2), "Nonempty songs trim to two channels unless already mono");
    }
    // Empty packed patterns use no cell bytes: a channel edit can save zero bytes.
    source.channels = 4; source.patterns = {pattern(1,4)}; source.patterns[0].cells[0] = {49,0,0,0,0};
    auto result = edit::prepareTrim(source,options(8));
    check(result.changed(), "Actual channel transformations report change");
}

void mapsAndEmptySlots() {
    using namespace brick;
    auto source = fixture(); const auto off = edit::prepareTrim(source,options(0));
    check(!off.changed(), "All options off is a true no-op");
    for (int id = 0; id < 256; ++id) check(off.ids.patternMap[std::size_t(id)] == id, "Pattern maps are identity outside cleanup scope");
    for (int id = 0; id < 129; ++id) {
        check(off.ids.instrumentMap[std::size_t(id)] == id, "Instrument maps include identity for implicit slots");
        for (int s = 0; s < 16; ++s) check(off.sampleMap[std::size_t(id)][std::size_t(s)] == s, "All sample slots are identity with sample cleanup disabled");
    }
    Document empty; empty.channels = 4; empty.orders = {0}; empty.instruments.emplace_back();
    empty.instruments[0].name = "VIRTUAL SAMPLE ZERO";
    const auto virtualZero = edit::prepareTrim(empty,options(4));
    check(!virtualZero.changed() && virtualZero.document.instruments[0].samples.empty() && virtualZero.sampleMap[1][0] == 0,
          "Zero-sample instruments retain their valid implicit silent sample zero");
    empty.instruments[0].samples.resize(3); empty.instruments[0].noteToSample.fill(2);
    empty.instruments[0].samples[2].name = "MAPPED EMPTY";
    const auto mappedEmpty = edit::prepareTrim(empty,options(4));
    check(mappedEmpty.samplesRemoved == 2 && mappedEmpty.samplesRenumbered == 1 && mappedEmpty.document.instruments[0].samples.size() == 1 &&
          mappedEmpty.document.instruments[0].samples[0].name == "MAPPED EMPTY" && mappedEmpty.document.instruments[0].noteToSample[95] == 0,
          "A mapped empty materialized sample is retained and compacted without erasing its name");
    empty.patterns.clear(); empty.orders = {255,255};
    const auto omitted = edit::prepareTrim(empty,options(1));
    check(omitted.changed() && omitted.beforeBytes == omitted.afterBytes && omitted.document.patterns.empty() && omitted.document.orders[0] == 0 && omitted.ids.patternMap[255] == 0,
          "Omitted-pattern ID changes report change even when encoded sizes are identical");
}

void precisionAndValidation() {
    using namespace brick;
    Document source; source.orders = {0}; source.instruments.resize(1); source.instruments[0].samples = {sample()};
    auto& pcm = source.instruments[0].samples[0]; pcm.reserved = 0xAD;
    const auto before = io::encodeXm(source); const auto result = edit::prepareTrim(source,options(32));
    const std::vector<std::int16_t> expected{-32768,-32768,-512,-256,-256,-256,0,0,0,256,256,32512};
    check(result.document.instruments[0].samples[0].pcm == expected && !result.document.instruments[0].samples[0].sixteenBit && result.samplesConverted == 1,
          "8-bit conversion uses FT2 high-byte flooring across negative values, zero and endpoints");
    check(result.document.instruments[0].samples[0].reserved == 0, "8-bit raw PCM clears the otherwise misleading ADPCM sentinel");
    auto restored = result.document; edit::DocumentDelta::between(source,result.document).undo(restored);
    check(io::encodeXm(restored) == before && restored.instruments[0].samples[0].reserved == 0xAD, "Undo restores precision and the original reserved byte");
    for (int type : {0,1,2,3}) {
        pcm = sample(false,type); const auto trimmed = edit::prepareTrim(source,options(16));
        check(trimmed.document.instruments[0].samples[0].pcm.size() == (type ? 4u : 5u), "Only active loop tails are removed, including FT2 loop type three");
    }
    pcm = {}; const auto empty = edit::prepareTrim(source,options(32));
    check(!empty.changed() && empty.document.instruments[0].samples[0].sixteenBit, "Empty 16-bit samples are not converted");
    source.instruments[0].noteToSample[95] = 15;
    const auto malformed = source;
    reject([&]{edit::prepareTrim(source);}, "Codec-invalid missing sample references are rejected rather than remapped to audible samples");
    check(edit::DocumentDelta::between(source,malformed).empty(), "Rejected missing-map calculation leaves source unchanged");
    source = fixture(); source.instruments[0].samples[0].loopType = 1; source.instruments[0].samples[0].loopLength = 0;
    const auto invalid = source;
    reject([&]{edit::prepareTrim(source);}, "Invalid data is validated even if its instrument would be discarded");
    check(edit::DocumentDelta::between(source,invalid).empty(), "Late invalid source data is never partially changed");
    reject([&]{edit::prepareTrim(source,options(0));}, "All-off calculation still validates source XM data");
}
}

int main() {
    try {
        allCombinations(); channelEdges(); mapsAndEmptySlots(); precisionAndValidation();
        std::cout << "Trim passed: " << checks << " checks\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
