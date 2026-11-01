#include "model/SampleChop.hpp"
#include "text/Utf8.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace brick::edit {
namespace {
constexpr std::size_t maxFrames = 32u * 1024u * 1024u;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void checkNotes(int firstNote, std::size_t count) {
    require(count >= 1 && count <= 16, "Chop count must be 1..16");
    require(firstNote >= 1 && firstNote <= 96 && count <= std::size_t(97 - firstNote),
            "Chop trigger notes must fit C-0 through B-7");
}

std::uint64_t energy(std::int16_t value) {
    const auto signedValue = std::int64_t(value);
    return std::uint64_t(signedValue * signedValue);
}

struct AttackRise {
    double score = 0.0;
    bool clear = false;
};

std::size_t nearbyAttack(const Sample& source, FrameRange range, std::size_t grid,
                         std::size_t radius, double rate) {
    if (!radius) return grid;
    const auto window = std::max<std::size_t>(2, std::size_t(std::ceil(rate * 0.003)));
    const auto baseline = window * 4;
    // Three neighbouring positions are needed to locate a local peak in the
    // envelope rise. All analysis stays inside the user's selected PCM.
    if (range.last - range.first <= baseline + window + 2) return grid;
    const auto first = std::max(grid - radius, range.first + baseline + 1);
    const auto last = std::min(grid + radius, range.last - window - 1);
    if (first > last) return grid;

    const auto start = first - 1;
    std::uint64_t recent = 0, prior = 0, after = 0;
    for (auto frame = start - baseline; frame < start; ++frame) prior += energy(source.pcm[frame]);
    for (auto frame = start - window; frame < start; ++frame) recent += energy(source.pcm[frame]);
    for (auto frame = start; frame < start + window; ++frame) after += energy(source.pcm[frame]);
    auto rise = [&] {
        const double beforeMean = double(recent) / double(window);
        const double baselineMean = double(prior) / double(baseline);
        const double afterMean = double(after) / double(window);
        const double difference = afterMean - beforeMean;
        // A roughly 6 dB energy-envelope rise must exceed both the recent
        // sound and a longer baseline. Ordinary waveform peaks are not used
        // as cut points; weak noise and steady tones retain the equal grid.
        return AttackRise{difference, afterMean >= 256.0 * 256.0 &&
            afterMean >= 4.0 * baselineMean && afterMean >= 4.0 * beforeMean &&
            difference >= 128.0 * 128.0};
    };
    auto advance = [&](std::size_t frame) {
        prior = prior - energy(source.pcm[frame - baseline]) + energy(source.pcm[frame]);
        recent = recent - energy(source.pcm[frame - window]) + energy(source.pcm[frame]);
        after = after - energy(source.pcm[frame]) + energy(source.pcm[frame + window]);
    };
    auto previous = rise();
    advance(start);
    auto current = rise();
    std::size_t best = grid, bestDistance = std::numeric_limits<std::size_t>::max();
    double bestScore = 0.0;
    for (auto frame = first; frame <= last; ++frame) {
        advance(frame);
        const auto next = rise();
        // The rightmost point of a flat peak identifies a short impulse's
        // leading edge, rather than a position one window ahead of it.
        if (current.clear && current.score >= previous.score && current.score > next.score) {
            const auto distance = frame > grid ? frame - grid : grid - frame;
            if (distance < bestDistance || (distance == bestDistance && current.score > bestScore)) {
                best = frame;bestDistance = distance;bestScore = current.score;
            }
        }
        previous = current;current = next;
    }
    return best;
}

void checkChop(const ChopResult& chop) {
    checkNotes(chop.firstNote, chop.slices.size());
    require(chop.boundaries.size() == chop.slices.size() + 1, "Chop boundaries do not match its slices");
    for (std::size_t n = 0; n < chop.slices.size(); ++n) {
        require(chop.boundaries[n] < chop.boundaries[n + 1], "Chop slices must be nonempty and ordered");
        require(chop.slices[n].pcm.size() == chop.boundaries[n + 1] - chop.boundaries[n],
                "Chop slice length does not match its boundaries");
    }
}
} // namespace

ChopResult chopSample(const Sample& source, FrameRange range, const ChopOptions& options) {
    require(range.first < range.last && range.last <= source.pcm.size(), "Select a nonempty PCM range to chop");
    require(range.last - range.first <= maxFrames, "Chop exceeds the 32M-frame memory limit");
    checkNotes(options.firstNote, std::size_t(options.count));
    const auto count = std::size_t(options.count), length = range.last - range.first;
    require(count <= length, "Chop needs at least one PCM frame per slice");
    require(source.volume <= 64, "Sample volume is outside the XM range");
    for (std::size_t n = 0; n < count; ++n) {
        const int tuning = int(source.relativeNote) - (options.firstNote + int(n) - 49);
        require(tuning >= -128 && tuning <= 127, "Chop trigger notes would exceed XM sample tuning range");
    }
    if (!source.sixteenBit)
        for (auto frame = range.first; frame < range.last; ++frame)
            require(source.pcm[frame] % 256 == 0, "PCM does not match its eight-bit depth");

    ChopResult result;result.firstNote = options.firstNote;
    result.boundaries.reserve(count + 1);result.slices.reserve(count);
    for (std::size_t n = 0; n <= count; ++n) result.boundaries.push_back(range.first + length * n / count);
    if (options.snapToTransients) {
        const auto grid = result.boundaries;
        const double rate = 8363.0 * std::exp2((double(source.relativeNote) + double(source.finetune) / 128.0) / 12.0);
        const auto maxRadius = std::size_t(rate * 0.025);
        for (std::size_t n = 1; n < count; ++n) {
            const auto radius = std::min(maxRadius, std::min(grid[n] - grid[n - 1], grid[n + 1] - grid[n]) / 4);
            result.boundaries[n] = nearbyAttack(source, range, grid[n], radius, rate);
            if (result.boundaries[n] != grid[n]) ++result.snappedBoundaries;
        }
    }
    for (std::size_t n = 0; n < count; ++n) {
        Sample slice;
        const auto suffix = std::string(" ") + (n < 9 ? "0" : "") + std::to_string(n + 1);
        slice.name = text::truncateBytes(source.name.empty() ? std::string("SLICE") : source.name, 22 - suffix.size()) + suffix;
        slice.sixteenBit = source.sixteenBit;slice.volume = source.volume;slice.panning = source.panning;
        slice.finetune = source.finetune;slice.reserved = source.reserved;
        slice.relativeNote = static_cast<std::int8_t>(int(source.relativeNote) - (options.firstNote + int(n) - 49));
        slice.pcm.assign(source.pcm.begin() + result.boundaries[n], source.pcm.begin() + result.boundaries[n + 1]);
        result.slices.push_back(std::move(slice));
    }
    return result;
}

int emptySampleSlots(const Instrument& instrument) {
    require(instrument.samples.size() <= 16, "Instrument exceeds the 16-sample XM limit");
    return 16 - int(std::count_if(instrument.samples.begin(), instrument.samples.end(),
        [](const Sample& sample){return !sample.pcm.empty();}));
}

ChopPlacement placeChop(const Instrument& instrument, const ChopResult& chop, bool replaceAll) {
    checkChop(chop);
    const auto available = emptySampleSlots(instrument);
    require(replaceAll || std::size_t(available) >= chop.slices.size(), "Not enough empty sample slots; choose a new or replacement instrument");
    ChopPlacement result;result.replaced = replaceAll;
    if (!replaceAll) result.instrument = instrument;
    result.slots.reserve(chop.slices.size());
    std::size_t nextSlice = 0;
    for (int slot = 0; slot < 16 && nextSlice < chop.slices.size(); ++slot) {
        if (std::size_t(slot) < result.instrument.samples.size() && !result.instrument.samples[slot].pcm.empty()) continue;
        if (result.instrument.samples.size() <= std::size_t(slot)) result.instrument.samples.resize(std::size_t(slot + 1));
        result.instrument.samples[std::size_t(slot)] = chop.slices[nextSlice];
        result.instrument.noteToSample[std::size_t(chop.firstNote - 1) + nextSlice] = std::uint8_t(slot);
        result.slots.push_back(slot);++nextSlice;
    }
    return result;
}

} // namespace brick::edit
