#include "model/TrackerExtras.hpp"

#include <algorithm>
#include <type_traits>
#include <utility>

namespace brick::edit {
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool blank(const Cell& cell) {
    return !(cell.note || cell.instrument || cell.volume || cell.effect || cell.param);
}
void checkPattern(const Pattern& pattern, std::size_t channels) {
    require(channels >= 1 && channels <= 32, "Channel count must be 1..32");
    require(pattern.rows >= 1 && pattern.rows <= 256, "Pattern length must be 1..256");
    require(pattern.cells.size() == std::size_t(pattern.rows) * channels,
            "Pattern storage dimensions are invalid");
}
std::size_t instrumentIndex(const Document& document, int oneBased, bool allowMissing = false) {
    require(document.instruments.size() <= 128, "Maximum 128 instruments");
    require(oneBased >= 1 && oneBased <= 128, "Instrument number must be 01..80");
    const auto index = static_cast<std::size_t>(oneBased - 1);
    require(allowMissing || index < document.instruments.size(), "Instrument slot does not exist");
    return index;
}
std::size_t sampleIndex(const Instrument& instrument, int zeroBased, bool allowMissing = false) {
    require(instrument.samples.size() <= 16, "Maximum 16 samples per instrument");
    require(zeroBased >= 0 && zeroBased < 16, "Sample slot must be 00..0F");
    const auto index = static_cast<std::size_t>(zeroBased);
    require(allowMissing || index < instrument.samples.size(), "Sample slot does not exist");
    return index;
}
void checkSample(const Sample& sample) {
    require(sample.pcm.size() <= 32u * 1024u * 1024u, "Sample exceeds the 32M-frame memory limit");
    require(sample.volume <= 64, "Sample volume exceeds 64");
    require(sample.loopType <= 3, "Invalid sample loop type");
    if (sample.loopType)
        require(sample.loopLength > 0 && sample.loopStart < sample.pcm.size() &&
                std::uint64_t(sample.loopStart) + sample.loopLength <= sample.pcm.size(),
                "Sample loop is outside PCM data");
    if (!sample.sixteenBit) {
        require(sample.reserved != 0xAD, "ModPlug ADPCM sample extension is unsupported");
        for (const auto value : sample.pcm)
            require(value % 256 == 0, "Eight-bit sample contains higher precision PCM");
    }
}
void checkInstrument(const Instrument& instrument) {
    require(instrument.samples.size() <= 16, "Maximum 16 samples per instrument");
    for (const auto index : instrument.noteToSample)
        require(index < std::max<std::size_t>(1, instrument.samples.size()),
                "Instrument key map references a missing sample");
    for (const auto& sample : instrument.samples) checkSample(sample);
    // Names, envelopes, auto-vibrato and extension bytes are copied verbatim.
    // Their codec validation belongs to import/save, not a slot-content edit.
}
static_assert(std::is_nothrow_move_constructible<Instrument>::value &&
              std::is_nothrow_move_assignable<Instrument>::value,
              "Instrument replacement must commit without throwing");
static_assert(std::is_nothrow_move_assignable<Pattern>::value,
              "Pattern replacement must commit without throwing");
static_assert(std::is_nothrow_move_constructible<Sample>::value &&
              std::is_nothrow_move_assignable<Sample>::value,
              "Sample replacement must commit without throwing");
static_assert(std::is_nothrow_default_constructible<Instrument>::value &&
              std::is_nothrow_default_constructible<Sample>::value,
              "Empty slot growth must not throw after allocation");
} // namespace

Cell mergeCell(const Cell& destination, const Cell& source, const PasteMask& mask) {
    Cell result = destination;
    if (mask.note && (!mask.skipEmpty || source.note)) result.note = source.note;
    if (mask.instrument && (!mask.skipEmpty || source.instrument)) result.instrument = source.instrument;
    if (mask.volume && (!mask.skipEmpty || source.volume)) result.volume = source.volume;
    const bool hasEffect = source.effect != 0 || source.param != 0;
    if (mask.effect && (!mask.skipEmpty || hasEffect)) result.effect = source.effect;
    if (mask.param && (!mask.skipEmpty || hasEffect)) result.param = source.param;
    return result;
}

void pasteMaskedBlock(Pattern& pattern, std::size_t channels, std::size_t row,
                      std::size_t channel, const PatternBlock& block, const PasteMask& mask) {
    checkPattern(pattern, channels);
    require(block.rows >= 1 && block.rows <= 256 && block.channels >= 1 && block.channels <= 32 &&
            block.cells.size() == block.rows * block.channels, "Pattern clipboard dimensions are invalid");
    require(row < pattern.rows && block.rows <= pattern.rows - row &&
            channel < channels && block.channels <= channels - channel,
            "Pattern clipboard does not fit; extend the pattern or move the cursor");
    require(block.fieldMasks.empty() || block.fieldMasks.size() == block.channels,
            "Pattern clipboard field masks do not match its channels");
    for (const auto fields : block.fieldMasks)
        require((fields & ~31u) == 0, "Pattern clipboard field mask is invalid");
    // All throwing validation precedes the first write. Cell merges allocate nothing.
    for (std::size_t r = 0; r < block.rows; ++r)
        for (std::size_t c = 0; c < block.channels; ++c) {
            auto& destination = pattern.cells[(row + r) * channels + channel + c];
            const auto fields = block.fieldMasks.empty() ? 31 : block.fieldMasks[c];
            const PasteMask selected{mask.note && bool(fields & 1), mask.instrument && bool(fields & 2),
                                     mask.volume && bool(fields & 4), mask.effect && bool(fields & 8),
                                     mask.param && bool(fields & 16), mask.skipEmpty};
            destination = mergeCell(destination, block.cells[r * block.channels + c], selected);
        }
}

void expandPattern(Pattern& pattern, std::size_t channels, bool allowDiscard) {
    checkPattern(pattern, channels);
    const auto nextRows = std::min<std::size_t>(std::size_t(pattern.rows) * 2, 256);
    const auto retainedRows = (nextRows + 1) / 2;
    if (!allowDiscard)
        for (std::size_t r = retainedRows; r < pattern.rows; ++r)
            for (std::size_t c = 0; c < channels; ++c)
                require(blank(pattern.cells[r * channels + c]),
                        "Expand would discard nonempty rows beyond the 256-row limit");
    std::vector<Cell> result(nextRows * channels);
    for (std::size_t r = 0; r < retainedRows; ++r)
        std::copy_n(pattern.cells.begin() + r * channels, channels, result.begin() + r * 2 * channels);
    pattern.cells.swap(result);
    pattern.rows = static_cast<std::uint16_t>(nextRows);
}

void shrinkPattern(Pattern& pattern, std::size_t channels, bool allowDiscard) {
    checkPattern(pattern, channels);
    if (!allowDiscard)
        for (std::size_t r = 1; r < pattern.rows; r += 2)
            for (std::size_t c = 0; c < channels; ++c)
                require(blank(pattern.cells[r * channels + c]),
                        "Shrink would discard nonempty odd rows");
    const auto nextRows = (std::size_t(pattern.rows) + 1) / 2;
    std::vector<Cell> result(nextRows * channels);
    for (std::size_t r = 0; r < nextRows; ++r)
        std::copy_n(pattern.cells.begin() + r * 2 * channels, channels, result.begin() + r * channels);
    pattern.cells.swap(result);
    pattern.rows = static_cast<std::uint16_t>(nextRows);
}

void clearPatternSlot(Document& document, int patternZeroBased) {
    require(document.patterns.size() <= 256, "Maximum 256 patterns");
    require(patternZeroBased >= 0 && patternZeroBased < 256, "Pattern number must be 00..FF");
    const auto index = static_cast<std::size_t>(patternZeroBased);
    if (index >= document.patterns.size()) return;
    auto& pattern = document.patterns[index];
    checkPattern(pattern, document.channels);
    std::fill(pattern.cells.begin(), pattern.cells.end(), Cell{});
}

void clearInstrumentSlot(Document& document, int instrumentOneBased) {
    const auto index = instrumentIndex(document, instrumentOneBased, true);
    if (index < document.instruments.size()) document.instruments[index] = Instrument{};
}

void clearSampleSlot(Document& document, int instrumentOneBased, int sampleZeroBased) {
    const auto instrument = instrumentIndex(document, instrumentOneBased, true);
    const Instrument emptyInstrument;
    const bool exists = instrument < document.instruments.size();
    // Validate both numbers even when the owning instrument has not been created.
    const auto sample = sampleIndex(exists ? document.instruments[instrument] : emptyInstrument,
                                    sampleZeroBased, true);
    if (exists && sample < document.instruments[instrument].samples.size())
        document.instruments[instrument].samples[sample] = Sample{};
}

void replacePattern(Document& document, int destinationZeroBased,
                    const Pattern& snapshot, int sourceChannels) {
    require(document.patterns.size() <= 256, "Maximum 256 patterns");
    require(destinationZeroBased >= 0 && destinationZeroBased < 256, "Pattern number must be 00..FF");
    require(sourceChannels >= 1 && sourceChannels <= 32, "Pattern clipboard channel count must be 1..32");
    checkPattern(snapshot, static_cast<std::size_t>(sourceChannels));
    require(sourceChannels == document.channels, "Pattern clipboard channel count does not match the song");
    const auto destination = static_cast<std::size_t>(destinationZeroBased);
    Pattern copy = snapshot; // Snapshot before growth: the source may belong to this document.
    if (destination < document.patterns.size()) {
        document.patterns[destination] = std::move(copy);
    } else {
        // Prepare every gap before committing: a failed allocation leaves the
        // document intact, including its original count of materialized slots.
        auto replacement = document.patterns;
        replacement.reserve(destination + 1);
        while (replacement.size() < destination) {
            Pattern empty;
            empty.cells.resize(std::size_t(empty.rows) * document.channels);
            replacement.push_back(std::move(empty));
        }
        replacement.push_back(std::move(copy));
        document.patterns.swap(replacement);
    }
}

void replaceInstrument(Document& document, int destinationOneBased, const Instrument& snapshot) {
    const auto destination = instrumentIndex(document, destinationOneBased, true);
    checkInstrument(snapshot);
    Instrument copy = snapshot; // Snapshot first: callers may reference this same document.
    // resize has the strong exception guarantee for these nonthrowing element
    // constructors/moves. After it succeeds, committing the snapshot cannot fail.
    if (destination >= document.instruments.size()) document.instruments.resize(destination + 1);
    document.instruments[destination] = std::move(copy);
}

void copyInstrument(Document& document, int sourceOneBased, int destinationOneBased) {
    const auto source = instrumentIndex(document, sourceOneBased);
    replaceInstrument(document, destinationOneBased, document.instruments[source]);
}

void swapInstruments(Document& document, int firstOneBased, int secondOneBased) {
    const auto first = instrumentIndex(document, firstOneBased);
    const auto second = instrumentIndex(document, secondOneBased);
    if (first != second) std::swap(document.instruments[first], document.instruments[second]);
}

void replaceSample(Document& document, int destinationInstrumentOneBased,
                   int destinationSampleZeroBased, const Sample& snapshot) {
    const auto instrument = instrumentIndex(document, destinationInstrumentOneBased, true);
    Instrument emptyInstrument;
    const bool exists = instrument < document.instruments.size();
    const auto destination = sampleIndex(exists ? document.instruments[instrument] : emptyInstrument,
                                         destinationSampleZeroBased, true);
    checkSample(snapshot);
    Sample copy = snapshot; // Snapshot an aliased source before either vector grows.
    if (exists) {
        auto& samples = document.instruments[instrument].samples;
        if (destination >= samples.size()) samples.resize(destination + 1);
        samples[destination] = std::move(copy);
    } else {
        // Allocate the sample slots before growing the document, so failure at
        // either allocation cannot leave behind a partially created instrument.
        emptyInstrument.samples.resize(destination + 1);
        emptyInstrument.samples[destination] = std::move(copy);
        document.instruments.resize(instrument + 1);
        document.instruments[instrument] = std::move(emptyInstrument);
    }
}

void copySample(Document& document, int sourceInstrumentOneBased, int sourceSampleZeroBased,
                int destinationInstrumentOneBased, int destinationSampleZeroBased) {
    const auto sourceInstrument = instrumentIndex(document, sourceInstrumentOneBased);
    const auto sourceSample = sampleIndex(document.instruments[sourceInstrument], sourceSampleZeroBased);
    replaceSample(document, destinationInstrumentOneBased, destinationSampleZeroBased,
                  document.instruments[sourceInstrument].samples[sourceSample]);
}

void swapSamples(Document& document, int firstInstrumentOneBased, int firstSampleZeroBased,
                 int secondInstrumentOneBased, int secondSampleZeroBased) {
    const auto firstInstrument = instrumentIndex(document, firstInstrumentOneBased);
    const auto secondInstrument = instrumentIndex(document, secondInstrumentOneBased);
    const auto firstSample = sampleIndex(document.instruments[firstInstrument], firstSampleZeroBased);
    const auto secondSample = sampleIndex(document.instruments[secondInstrument], secondSampleZeroBased);
    if (firstInstrument != secondInstrument || firstSample != secondSample)
        std::swap(document.instruments[firstInstrument].samples[firstSample],
                  document.instruments[secondInstrument].samples[secondSample]);
}

} // namespace brick::edit
