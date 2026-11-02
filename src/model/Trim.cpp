#include "model/Trim.hpp"

#include "io/Codec.hpp"
#include "model/Minimise.hpp"

#include <algorithm>
#include <utility>

namespace brick::edit {
namespace {
void removeUnusedSamples(TrimResult& result) {
    for (std::size_t oldId = 1; oldId < result.sampleMap.size(); ++oldId) {
        const int newId = result.ids.instrumentMap[oldId];
        if (newId < 1 || std::size_t(newId) > result.document.instruments.size()) continue;
        auto& instrument = result.document.instruments[std::size_t(newId - 1)];
        if (instrument.samples.empty()) continue; // Implicit silent sample zero.
        std::array<bool, 16> used{};
        for (auto index : instrument.noteToSample) used[index] = true;
        auto& map = result.sampleMap[oldId];
        std::vector<Sample> retained;
        retained.reserve(instrument.samples.size());
        for (std::size_t index = 0; index < instrument.samples.size(); ++index) {
            if (!used[index]) { map[index] = -1; ++result.samplesRemoved; continue; }
            map[index] = int(retained.size());
            if (map[index] != int(index)) ++result.samplesRenumbered;
            retained.push_back(std::move(instrument.samples[index]));
        }
        for (auto& index : instrument.noteToSample) index = static_cast<std::uint8_t>(map[index]);
        instrument.samples.swap(retained);
    }
}

void removeTrailingChannels(TrimResult& result) {
    auto& document = result.document;
    const auto oldChannels = document.channels;
    int highest = -1;
    for (const auto& pattern : document.patterns)
        for (std::size_t i = 0; i < pattern.cells.size(); ++i) {
            const auto& cell = pattern.cells[i];
            if (cell.note || cell.instrument || cell.volume || cell.effect || cell.param)
                highest = std::max(highest, int(i % oldChannels));
        }
    if (highest < 0) return;
    const auto channels = static_cast<std::uint16_t>(std::min(int(oldChannels), std::max(2, (highest + 2) & ~1)));
    if (channels == oldChannels) return;
    for (auto& pattern : document.patterns) {
        std::vector<Cell> retained;
        retained.reserve(std::size_t(pattern.rows) * channels);
        for (std::size_t row = 0; row < pattern.rows; ++row) {
            const auto first = pattern.cells.begin() + row * oldChannels;
            retained.insert(retained.end(), first, first + channels);
        }
        pattern.cells.swap(retained);
    }
    result.channelsRemoved = oldChannels - channels;
    document.channels = channels;
}
} // namespace

bool TrimResult::changed() const noexcept {
    return ids.changed() || samplesRemoved || samplesRenumbered || channelsRemoved || samplesTrimmed || samplesConverted;
}

TrimResult prepareTrim(const Document& source, const TrimOptions& options) {
    // Validate even discarded data first, and measure using the actual writer.
    TrimResult result;
    result.beforeBytes = io::encodeXm(source).size();
    result.document = source;
    for (std::size_t i = 0; i < result.ids.patternMap.size(); ++i) result.ids.patternMap[i] = int(i);
    for (std::size_t i = 0; i < result.ids.instrumentMap.size(); ++i) result.ids.instrumentMap[i] = int(i);
    for (auto& instrument : result.sampleMap)
        for (std::size_t i = 0; i < instrument.size(); ++i) instrument[i] = int(i);

    if (options.unusedPatterns || options.unusedInstruments) {
        const auto scope = options.unusedPatterns
            ? (options.unusedInstruments ? CleanupScope::Both : CleanupScope::Patterns)
            : CleanupScope::Instruments;
        result.ids = cleanupUnused(result.document, scope);
        for (std::size_t id = 1; id <= source.instruments.size(); ++id)
            if (result.ids.instrumentMap[id] < 0) result.sampleMap[id].fill(-1);
    }
    if (options.unusedSamples) removeUnusedSamples(result);
    if (options.unusedChannels) removeTrailingChannels(result);
    if (options.sampleTails) result.samplesTrimmed = int(minimise(result.document).samplesChanged);
    if (options.convertTo8Bit)
        for (auto& instrument : result.document.instruments)
            for (auto& sample : instrument.samples) {
                if (!sample.sixteenBit || sample.pcm.empty()) continue;
                for (auto& value : sample.pcm) {
                    // C++17 signed right shift is implementation-defined. This
                    // portable floor division has the same high-byte result.
                    const int highByte = value >= 0 ? int(value) / 256 : (int(value) - 255) / 256;
                    value = static_cast<std::int16_t>(highByte * 256);
                }
                sample.sixteenBit = false;
                // In 8-bit XM only, 0xAD means compressed ADPCM rather than
                // ordinary delta PCM. The converted sample is uncompressed.
                if (sample.reserved == 0xAD) sample.reserved = 0;
                ++result.samplesConverted;
            }
    result.afterBytes = io::encodeXm(result.document).size();
    return result;
}

} // namespace brick::edit
