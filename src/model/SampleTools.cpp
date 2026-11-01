#include "model/SampleTools.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace brick::edit {
namespace {
constexpr std::size_t maxFrames = 32u * 1024u * 1024u;
constexpr std::uint64_t maxEchoWork = 128u * 1024u * 1024u;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void checkPcm(const std::vector<std::int16_t>& pcm, bool sixteenBit) {
    require(pcm.size() <= maxFrames, "Sample exceeds the 32M-frame memory limit");
    if (!sixteenBit)
        for (const auto value : pcm)
            require(value % 256 == 0, "PCM does not match its eight-bit depth");
}

void checkSample(const Sample& sample, bool allowEmpty = false) {
    checkPcm(sample.pcm, sample.sixteenBit);
    require(allowEmpty || !sample.pcm.empty(), "Sample is empty");
    require(sample.loopType <= 3, "Sample loop type is invalid");
    // XM may retain unused markers beyond PCM while looping is disabled.
    if (sample.loopType)
        require(sample.loopLength > 0 && sample.loopStart < sample.pcm.size() &&
                std::uint64_t(sample.loopStart) + sample.loopLength <= sample.pcm.size(),
                "Enabled loop is empty or outside PCM data");
}

std::int16_t quantize(double value, bool sixteenBit) {
    value = std::clamp(value, -32768.0, 32767.0);
    if (sixteenBit) return static_cast<std::int16_t>(std::lround(value));
    return static_cast<std::int16_t>(std::clamp(std::lround(value / 256.0), -128L, 127L) * 256);
}

bool matchingGeometry(const Sample& a, const Sample& b) {
    return a.pcm.size() == b.pcm.size() && a.relativeNote == b.relativeNote &&
           a.finetune == b.finetune && a.loopType == b.loopType &&
           (!a.loopType || (a.loopStart == b.loopStart && a.loopLength == b.loopLength));
}
} // namespace

void crossfadeLoop(Sample& sample, FrameRange selection) {
    checkSample(sample);
    require(sample.loopType == 1, "Crossfade needs a forward loop");
    require(selection.first < selection.last && selection.first <= sample.loopStart &&
            selection.last > sample.loopStart && selection.last <= sample.pcm.size(),
            "Select a PCM range across the first loop point");
    const auto length = selection.last - selection.first;
    require(length >= 2 && length <= sample.loopLength,
            "Crossfade range must be 2 frames or more and no longer than the loop");
    require(selection.last <= sample.pcm.size() - sample.loopLength,
            "Crossfade needs the same range after the second loop point");

    // Work from the original PCM even if a range touches its counterpart.
    // At wrap-around both loop-boundary neighbourhoods then follow the same
    // transition from the original tail into the original loop head.
    auto result = sample.pcm;
    for (std::size_t offset = 0; offset < length; ++offset) {
        const auto first = selection.first + offset;
        const auto second = first + sample.loopLength;
        const double blend = double(offset) / double(length - 1);
        const auto value = quantize(double(sample.pcm[second]) * (1.0 - blend) +
                                    double(sample.pcm[first]) * blend, sample.sixteenBit);
        result[first] = value;
        result[second] = value;
    }
    sample.pcm.swap(result);
}

void resampleSample(Sample& sample, int semitones) {
    checkSample(sample);
    require(semitones >= -36 && semitones <= 36, "Resample shift must be -36..36 semitones");
    const int relativeNote = int(sample.relativeNote) + semitones;
    require(relativeNote >= -128 && relativeNote <= 127,
            "Resampling would exceed XM relative-note range; original sample kept");
    if (!semitones) return;

    const double ratio = std::exp2(double(semitones) / 12.0);
    const double size = std::floor(double(sample.pcm.size()) * ratio);
    require(size >= 1.0, "Resampling would leave no PCM frames");
    require(size <= double(maxFrames), "Resampled sample exceeds the 32M-frame memory limit");
    const auto newSize = static_cast<std::size_t>(size);
    const auto start = static_cast<std::uint32_t>(std::min(size,
        std::floor(double(sample.loopStart) * ratio)));
    const auto end = static_cast<std::uint32_t>(std::min(size, std::floor(
        double(std::uint64_t(sample.loopStart) + sample.loopLength) * ratio)));
    require(end <= newSize && end >= start && (!sample.loopType || end > start),
            "Resampling would collapse the enabled loop; original sample kept");

    std::vector<std::int16_t> result(newSize);
    for (std::size_t frame = 0; frame < newSize; ++frame) {
        const auto source = std::min(static_cast<std::size_t>(double(frame) / ratio), sample.pcm.size() - 1);
        result[frame] = sample.pcm[source];
    }
    sample.pcm.swap(result);
    sample.relativeNote = static_cast<std::int8_t>(relativeNote);
    sample.loopStart = start;
    sample.loopLength = end - start;
}

void mixPcm(Sample& sample, std::size_t start, const PcmClip& clip, int sourcePercent) {
    checkSample(sample, true);
    checkPcm(clip.pcm, clip.sixteenBit);
    require(start <= sample.pcm.size(), "Mix start is outside the destination sample");
    require(!clip.pcm.empty(), "Copy PCM before mixing");
    require(sourcePercent >= 0 && sourcePercent <= 100, "Mix balance must be 0..100 percent");
    require(clip.pcm.size() <= maxFrames - start, "Mixed sample exceeds the 32M-frame memory limit");
    if (!sourcePercent) return;

    auto result = sample.pcm;
    result.resize(std::max(result.size(), start + clip.pcm.size()), 0);
    const double sourceGain = double(sourcePercent) / 100.0;
    for (std::size_t frame = 0; frame < clip.pcm.size(); ++frame) {
        const auto target = start + frame;
        const double destination = target < sample.pcm.size() ? sample.pcm[target] : 0.0;
        result[target] = quantize(destination * (1.0 - sourceGain) +
                                  double(clip.pcm[frame]) * sourceGain, sample.sixteenBit);
    }
    sample.pcm.swap(result);
}

void drawPcmPoint(Sample& sample, std::size_t frame, int amplitude) {
    // Avoid a full PCM scan for every gamepad movement in the drawing editor.
    require(sample.pcm.size() <= maxFrames && frame < sample.pcm.size(), "Draw frame is outside PCM data");
    require(amplitude >= -32768 && amplitude <= 32767, "PCM amplitude must be -32768..32767");
    sample.pcm[frame] = quantize(double(amplitude), sample.sixteenBit);
}

void drawPcmStroke(Sample& sample, std::size_t left, std::size_t frame,
                   std::size_t right, int amplitude) {
    require(sample.pcm.size() <= maxFrames && left <= frame && frame <= right &&
            right < sample.pcm.size(), "Draw control points are outside PCM data");
    require(amplitude >= -32768 && amplitude <= 32767, "PCM amplitude must be -32768..32767");
    const auto leftValue = sample.pcm[left], rightValue = sample.pcm[right];
    const auto value = quantize(double(amplitude), sample.sixteenBit);
    // Original neighbours are captured before writing; moving one point cannot
    // accumulate errors at the adjacent anchors or change unselected PCM.
    if (left < frame)
        for (std::size_t n = left; n < frame; ++n) {
            const double amount = double(n - left) / double(frame - left);
            sample.pcm[n] = quantize(leftValue * (1.0 - amount) + value * amount, sample.sixteenBit);
        }
    sample.pcm[frame] = value;
    if (frame < right)
        for (std::size_t n = frame + 1; n <= right; ++n) {
            const double amount = double(n - frame) / double(right - frame);
            sample.pcm[n] = quantize(value * (1.0 - amount) + rightValue * amount, sample.sixteenBit);
        }
}

void fillWavePreset(Sample& sample, FrameRange range, WavePreset preset, int amplitude,
                    std::uint32_t noiseSeed, int dutyEighths, int levels) {
    require(sample.pcm.size() <= maxFrames && range.first < range.last && range.last <= sample.pcm.size(),
            "Wave preset range is empty or outside PCM data");
    require(amplitude >= 0 && amplitude <= 32767, "Wave amplitude must be 0..32767");
    require(preset >= WavePreset::Sine && preset <= WavePreset::Noise, "Unknown wave preset");
    require(dutyEighths == 1 || dutyEighths == 2 || dutyEighths == 4 || dutyEighths == 6,
            "Pulse duty must be 1, 2, 4 or 6 eighths");
    require(levels == 16 || levels == 256, "Wave levels must be 16 or 256");
    const auto length = range.last - range.first;
    std::uint32_t noise = noiseSeed ? noiseSeed : 0x6d2b79f5u;
    constexpr double tau = 6.283185307179586476925286766559;
    for (std::size_t n = 0; n < length; ++n) {
        const double phase = double(n) / double(length);
        double value = 0;
        switch (preset) {
        case WavePreset::Sine: value = std::sin(phase * tau); break;
        case WavePreset::Triangle: value = 1.0 - 4.0 * std::abs(phase - 0.5); break;
        case WavePreset::Square: value = phase < double(dutyEighths) / 8.0 ? 1.0 : -1.0; break;
        case WavePreset::Saw: value = phase * 2.0 - 1.0; break;
        case WavePreset::Noise:
            noise ^= noise << 13; noise ^= noise >> 17; noise ^= noise << 5;
            value = (double(noise >> 16) - 32768.0) / 32768.0;
            break;
        }
        if (levels == 16 && preset != WavePreset::Square && preset != WavePreset::Noise)
            value = std::round((std::clamp(value, -1.0, 1.0) + 1.0) * 7.5) / 7.5 - 1.0;
        sample.pcm[range.first + n] = quantize(value * amplitude, sample.sixteenBit);
    }
}

std::size_t noisePresetFrames(NoisePreset preset) {
    require(preset == NoisePreset::Short || preset == NoisePreset::Long, "Unknown noise preset");
    return preset == NoisePreset::Short ? 127u : 32767u;
}

void fillNoisePreset(Sample& sample, FrameRange range, NoisePreset preset, int amplitude) {
    const auto length = noisePresetFrames(preset);
    require(sample.pcm.size() <= maxFrames && range.first < range.last && range.last <= sample.pcm.size(),
            "Noise preset range is empty or outside PCM data");
    require(range.last - range.first == length, "Noise preset needs one complete 127 or 32767-frame period");
    require(amplitude >= 0 && amplitude <= 32767, "Noise amplitude must be 0..32767");
    const unsigned topBit = preset == NoisePreset::Short ? 6u : 14u;
    std::uint32_t state = static_cast<std::uint32_t>(length);
    const auto high = quantize(double(amplitude), sample.sixteenBit);
    const auto low = quantize(-double(amplitude), sample.sixteenBit);
    for (std::size_t n = 0; n < length; ++n) {
        sample.pcm[range.first + n] = (state & 1u) ? low : high;
        // Adjacent low-bit XOR feedback gives a maximal-length sequence for
        // both widths. A fixed nonzero seed keeps preview and CREATE identical.
        const auto feedback = (state ^ (state >> 1u)) & 1u;
        state = (state >> 1u) | (feedback << topBit);
    }
}

void echoSample(Sample& sample, std::size_t delayFrames, int repeats,
                int decayPercent, bool extendTail) {
    checkSample(sample);
    require(repeats >= 0 && repeats <= 64, "Echo count must be 0..64");
    require(decayPercent >= 0 && decayPercent <= 100, "Echo level must be 0..100 percent");
    require(delayFrames >= 1 && delayFrames <= maxFrames, "Echo delay must be 1..32M PCM frames");
    if (!repeats || !decayPercent) return;

    const auto tail = std::uint64_t(delayFrames) * std::uint64_t(repeats);
    const auto wanted = std::uint64_t(sample.pcm.size()) + (extendTail ? tail : 0);
    require(wanted <= maxFrames, "Echo tail exceeds the 32M-frame memory limit");
    const auto newSize = static_cast<std::size_t>(wanted);
    const double decay = double(decayPercent) / 100.0;
    std::array<double, 65> gains{};
    gains[0] = 1.0;
    std::uint64_t work = 0;
    for (int tap = 1; tap <= repeats; ++tap) {
        gains[std::size_t(tap)] = gains[std::size_t(tap - 1)] * decay;
        const auto offset = std::uint64_t(delayFrames) * std::uint64_t(tap);
        if (offset < wanted) work += std::min<std::uint64_t>(sample.pcm.size(), wanted - offset);
    }
    require(work <= maxEchoWork, "Echo is too large; lower repeat count or sample length");
    std::vector<std::int16_t> result(newSize);
    // Evaluate from the original dry sample, never from clipped output. This
    // keeps repeated echoes consistent and quantizes only the final sum.
    for (std::size_t frame = 0; frame < newSize; ++frame) {
        double value = frame < sample.pcm.size() ? sample.pcm[frame] : 0.0;
        const auto firstTap = frame >= sample.pcm.size() ? (frame - sample.pcm.size()) / delayFrames + 1 : 1;
        const auto lastTap = std::min<std::size_t>(std::size_t(repeats), frame / delayFrames);
        for (auto tap = firstTap; tap <= lastTap; ++tap) {
            const auto offset = std::uint64_t(delayFrames) * std::uint64_t(tap);
            const auto source = frame - static_cast<std::size_t>(offset);
            value += double(sample.pcm[source]) * gains[tap];
        }
        result[frame] = quantize(value, sample.sixteenBit);
    }
    sample.pcm.swap(result);
}

void morphSamples(Instrument& instrument, std::size_t firstSlot, std::size_t lastSlot) {
    require(instrument.samples.size() <= 16 && firstSlot < lastSlot &&
            lastSlot < instrument.samples.size(), "Choose two existing samples in the same instrument");
    require(lastSlot - firstSlot >= 2, "Morph needs at least one sample slot between its endpoints");
    const auto& first = instrument.samples[firstSlot];
    const auto& last = instrument.samples[lastSlot];
    checkSample(first);
    checkSample(last);
    require(matchingGeometry(first, last),
            "Morph endpoints need equal PCM length, tuning and loop markers");
    const auto count = lastSlot - firstSlot - 1;
    require(first.pcm.size() <= maxFrames / count,
            "Generated morph samples exceed the combined 32M-frame memory limit");

    std::vector<Sample> result;
    result.reserve(count);
    for (std::size_t slot = firstSlot + 1; slot < lastSlot; ++slot) {
        Sample blend = first;
        blend.sixteenBit = first.sixteenBit || last.sixteenBit;
        const double amount = double(slot - firstSlot) / double(lastSlot - firstSlot);
        for (std::size_t frame = 0; frame < blend.pcm.size(); ++frame)
            blend.pcm[frame] = quantize(double(first.pcm[frame]) * (1.0 - amount) +
                                        double(last.pcm[frame]) * amount, blend.sixteenBit);
        result.push_back(std::move(blend));
    }
    // All allocation and validation finished. Sample's standard-allocator
    // string/vector moves are nonthrowing, so the group commits together.
    for (std::size_t offset = 0; offset < result.size(); ++offset)
        instrument.samples[firstSlot + offset + 1] = std::move(result[offset]);
}

} // namespace brick::edit
