#include "model/EditOperations.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace brick::edit {
namespace {
constexpr std::size_t maxFrames = 32u * 1024u * 1024u;
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
bool same(const Cell& a, const Cell& b) {
    return a.note == b.note && a.instrument == b.instrument && a.volume == b.volume &&
           a.effect == b.effect && a.param == b.param;
}
bool blank(const Cell& cell) { return same(cell, Cell{}); }
void checkPattern(const Pattern& pattern, std::size_t channels) {
    require(channels >= 1 && channels <= 32, "Channel count must be 1..32");
    require(pattern.rows >= 1 && pattern.rows <= 256, "Pattern length must be 1..256");
    require(pattern.cells.size() == std::size_t(pattern.rows) * channels, "Pattern storage dimensions are invalid");
}
void checkBlock(const Pattern& pattern, std::size_t channels, BlockRange range) {
    checkPattern(pattern, channels);
    require(range.firstRow < range.lastRow && range.lastRow <= pattern.rows &&
            range.firstChannel < range.lastChannel && range.lastChannel <= channels,
            "Pattern selection is outside the pattern");
    require(range.firstField < 5 && range.lastField >= 1 && range.lastField <= 5 &&
            (range.lastChannel - range.firstChannel > 1 || range.firstField < range.lastField),
            "Pattern selection fields are invalid");
}
void copyFields(Cell& destination, const Cell& source, std::uint8_t fields) {
    if (fields & 1) destination.note = source.note;
    if (fields & 2) destination.instrument = source.instrument;
    if (fields & 4) destination.volume = source.volume;
    if (fields & 8) destination.effect = source.effect;
    if (fields & 16) destination.param = source.param;
}
void checkRange(const Sample& sample, FrameRange range, bool allowEmpty = false) {
    require(range.first <= range.last && range.last <= sample.pcm.size() &&
            (allowEmpty || range.first != range.last), "Sample selection is empty or outside PCM data");
}
std::int16_t quantize(double value, bool sixteenBit) {
    value = std::clamp(value, -32768.0, 32767.0);
    if (sixteenBit) return static_cast<std::int16_t>(std::lround(value));
    return static_cast<std::int16_t>(std::clamp(std::lround(value / 256.0), -128L, 127L) * 256);
}
void resampleClipboard(const PcmClip& clip, double ratio, bool sixteenBit,
                       std::int16_t* destination, std::size_t frames) {
    // Windowed sinc interpolation, with a lower cutoff when reducing the rate
    // so frequencies above the destination Nyquist limit do not fold back.
    // A small shared lookup avoids trig calls for every tap on the handheld.
    constexpr int radius = 16, resolution = 512;
    static const auto kernel = [] {
        constexpr double pi = 3.14159265358979323846;
        std::array<double, radius * resolution + 1> values{};
        values[0] = 1.0;
        for (std::size_t i = 1; i < values.size(); ++i) {
            const double x = double(i) / resolution;
            values[i] = std::sin(pi * x) / (pi * x) *
                (0.5 + 0.5 * std::cos(pi * x / radius));
        }
        return values;
    }();
    const double cutoff = std::min(1.0, ratio);
    const double support = radius / cutoff;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const double position = double(frame) / ratio;
        const auto first = static_cast<std::size_t>(std::max(0.0, std::ceil(position - support)));
        const auto last = static_cast<std::size_t>(std::min(double(clip.pcm.size() - 1), std::floor(position + support)));
        double value = 0.0, total = 0.0;
        for (auto source = first; source <= last; ++source) {
            const double distance = std::abs(double(source) - position) * cutoff * resolution;
            if (distance >= radius * resolution) continue;
            const auto index = static_cast<std::size_t>(distance);
            const double fraction = distance - double(index);
            const double weight = kernel[index] * (1.0 - fraction) + kernel[index + 1] * fraction;
            value += double(clip.pcm[source]) * weight;
            total += weight;
        }
        // Normalize the truncated kernel at the clip edges, retaining DC level
        // without reading PCM outside the copied selection.
        destination[frame] = quantize(value / total, sixteenBit);
    }
}
void repairMarkers(Envelope& envelope) {
    require(envelope.count <= 12, "Envelope exceeds 12 points");
    if (!envelope.count) {
        envelope.flags = envelope.sustain = envelope.loopStart = envelope.loopEnd = 0;
        return;
    }
    const auto last = static_cast<std::uint8_t>(envelope.count - 1);
    envelope.sustain = std::min(envelope.sustain, last);
    envelope.loopStart = std::min(envelope.loopStart, last);
    envelope.loopEnd = std::clamp(envelope.loopEnd, envelope.loopStart, last);
}
void checkPoint(const Envelope& envelope, std::size_t index, EnvPoint point, bool insertion) {
    require(envelope.count <= 12 && index <= envelope.count && (insertion || index < envelope.count),
            "Envelope point index is invalid");
    require(point.x <= 32767 && point.y <= 64, "Envelope point must be tick 0..32767, value 0..64");
    if (index) require(point.x >= envelope.points[index - 1].x, "Envelope point must follow the preceding tick");
    const auto next = index + (insertion ? 0 : 1);
    if (next < envelope.count) require(point.x <= envelope.points[next].x, "Envelope point must precede the following tick");
}
bool same(const Envelope& a, const Envelope& b) {
    if (a.count != b.count || a.sustain != b.sustain || a.loopStart != b.loopStart ||
        a.loopEnd != b.loopEnd || a.flags != b.flags) return false;
    for (std::size_t i = 0; i < a.points.size(); ++i)
        if (a.points[i].x != b.points[i].x || a.points[i].y != b.points[i].y) return false;
    return true;
}
bool sameSampleMetadata(const Sample& a, const Sample& b) {
    return a.name == b.name && a.sixteenBit == b.sixteenBit && a.volume == b.volume && a.panning == b.panning &&
           a.loopType == b.loopType && a.finetune == b.finetune && a.relativeNote == b.relativeNote &&
           a.loopStart == b.loopStart && a.loopLength == b.loopLength && a.reserved == b.reserved;
}
bool sameInstrumentMetadata(const Instrument& a, const Instrument& b) {
    if (a.name != b.name || a.noteToSample != b.noteToSample || !same(a.volumeEnvelope, b.volumeEnvelope) ||
        !same(a.panningEnvelope, b.panningEnvelope) || a.vibratoType != b.vibratoType ||
        a.vibratoSweep != b.vibratoSweep || a.vibratoDepth != b.vibratoDepth || a.vibratoRate != b.vibratoRate ||
        a.fadeout != b.fadeout || a.extension != b.extension || a.samples.size() != b.samples.size()) return false;
    for (std::size_t i = 0; i < a.samples.size(); ++i)
        if (!sameSampleMetadata(a.samples[i], b.samples[i])) return false;
    return true;
}
Sample sampleMetadata(const Sample& sample) {
    Sample result;
    result.name = sample.name; result.sixteenBit = sample.sixteenBit;
    result.volume = sample.volume; result.panning = sample.panning; result.loopType = sample.loopType;
    result.finetune = sample.finetune; result.relativeNote = sample.relativeNote;
    result.loopStart = sample.loopStart; result.loopLength = sample.loopLength; result.reserved = sample.reserved;
    return result;
}
Instrument instrumentMetadata(const Instrument& instrument) {
    Instrument result;
    result.name = instrument.name; result.noteToSample = instrument.noteToSample;
    result.volumeEnvelope = instrument.volumeEnvelope; result.panningEnvelope = instrument.panningEnvelope;
    result.vibratoType = instrument.vibratoType; result.vibratoSweep = instrument.vibratoSweep;
    result.vibratoDepth = instrument.vibratoDepth; result.vibratoRate = instrument.vibratoRate;
    result.fadeout = instrument.fadeout; result.extension = instrument.extension;
    result.samples.reserve(instrument.samples.size());
    for (const auto& sample : instrument.samples) result.samples.push_back(sampleMetadata(sample));
    return result;
}
bool same(const Pattern& a, const Pattern& b) {
    return a.rows == b.rows && a.cells.size() == b.cells.size() &&
           std::equal(a.cells.begin(), a.cells.end(), b.cells.begin(), [](const Cell& x, const Cell& y) { return same(x, y); });
}
struct Header {
    std::string name;
    std::uint16_t channels, restart, speed, bpm;
    bool linear;
    std::vector<std::uint8_t> orders;
    std::size_t patterns, instruments;
    explicit Header(const Document& document) : name(document.name), channels(document.channels), restart(document.restart),
        speed(document.speed), bpm(document.bpm), linear(document.linear), orders(document.orders),
        patterns(document.patterns.size()), instruments(document.instruments.size()) {}
    bool matches(const Document& document) const {
        return name == document.name && channels == document.channels && restart == document.restart &&
            speed == document.speed && bpm == document.bpm && linear == document.linear && orders == document.orders &&
            patterns == document.patterns.size() && instruments == document.instruments.size();
    }
    void apply(Document& document) && {
        document.name = std::move(name); document.channels = channels; document.restart = restart;
        document.speed = speed; document.bpm = bpm; document.linear = linear; document.orders = std::move(orders);
    }
};
struct PatternDelta { std::size_t index; Pattern before, after; };
struct PcmDelta {
    std::size_t sampleIndex, first, beforeSize, afterSize;
    std::vector<std::int16_t> before, after;
};
struct InstrumentDelta {
    std::size_t index;
    Instrument before, after; // Metadata only. No complete PCM copies.
    std::vector<PcmDelta> pcm;
};
} // namespace

FrameRange wholeSample(const Sample& sample) { return {0, sample.pcm.size()}; }

void transformSample(Sample& sample, FrameRange range, SampleTransform operation, double gain) {
    checkRange(sample, range);
    require(std::isfinite(gain) && gain >= 0, "Sample gain must be finite and nonnegative");
    const auto count = range.last - range.first;
    if (operation == SampleTransform::Reverse) {
        std::reverse(sample.pcm.begin() + range.first, sample.pcm.begin() + range.last);
        const auto loopEnd = std::uint64_t(sample.loopStart) + sample.loopLength;
        // A fully enclosed loop follows its reversed PCM; partial intersections
        // retain their original positions and remain within the unchanged length.
        if (sample.loopType && sample.loopStart >= range.first && loopEnd <= range.last)
            sample.loopStart = static_cast<std::uint32_t>(range.first + range.last - loopEnd);
        return;
    }
    double dc = 0;
    if (operation == SampleTransform::Normalize) {
        int peak = 0;
        for (std::size_t i = range.first; i < range.last; ++i) peak = std::max(peak, std::abs(int(sample.pcm[i])));
        if (!peak) return;
        gain = (sample.sixteenBit ? 32767.0 : 32512.0) / peak;
    } else if (operation == SampleTransform::RemoveDc) {
        std::int64_t sum = 0;
        for (std::size_t i = range.first; i < range.last; ++i) sum += sample.pcm[i];
        dc = double(sum) / double(count);
    }
    for (std::size_t i = range.first; i < range.last; ++i) {
        double value = sample.pcm[i];
        switch (operation) {
        case SampleTransform::Normalize: case SampleTransform::Gain: value *= gain; break;
        case SampleTransform::FadeIn: value *= count > 1 ? double(i - range.first) / double(count - 1) : 0; break;
        case SampleTransform::FadeOut: value *= count > 1 ? double(range.last - 1 - i) / double(count - 1) : 0; break;
        case SampleTransform::Silence: value = 0; break;
        case SampleTransform::Invert: value = -value; break;
        case SampleTransform::RemoveDc: value -= dc; break;
        case SampleTransform::Reverse: break;
        }
        sample.pcm[i] = quantize(value, sample.sixteenBit);
    }
}

PcmClip copyPcm(const Sample& sample, FrameRange range) {
    checkRange(sample, range);
    return {{sample.pcm.begin() + range.first, sample.pcm.begin() + range.last},
        sample.sixteenBit, sample.relativeNote, sample.finetune};
}

std::size_t pastePcm(Sample& sample, FrameRange replaced, const PcmClip& clip) {
    checkRange(sample, replaced, true);
    const auto retained = sample.pcm.size() - (replaced.last - replaced.first);
    require(retained <= maxFrames && clip.pcm.size() <= maxFrames, "Edited sample exceeds the 32M-frame memory limit");
    if (!clip.sixteenBit)
        for (const auto value : clip.pcm) require(value % 256 == 0, "Clipboard PCM does not match its eight-bit depth");
    const int tuningDifference = (int(sample.relativeNote) - int(clip.relativeNote)) * 128 +
        int(sample.finetune) - int(clip.finetune);
    const double ratio = std::exp2(double(tuningDifference) / 1536.0);
    const double convertedSize = std::round(double(clip.pcm.size()) * ratio);
    require(clip.pcm.empty() || convertedSize >= 1.0, "Tuning conversion would leave no PCM frames");
    require(convertedSize <= double(maxFrames - retained), "Edited sample exceeds the 32M-frame memory limit");
    const auto inserted = static_cast<std::size_t>(convertedSize);
    std::vector<std::int16_t> result(retained + inserted);
    std::copy(sample.pcm.begin(), sample.pcm.begin() + replaced.first, result.begin());
    if (!tuningDifference)
        std::copy(clip.pcm.begin(), clip.pcm.end(), result.begin() + replaced.first);
    else if (inserted)
        resampleClipboard(clip, ratio, sample.sixteenBit || clip.sixteenBit,
            result.data() + replaced.first, inserted);
    std::copy(sample.pcm.begin() + replaced.last, sample.pcm.end(), result.begin() + replaced.first + inserted);
    const auto relocateStart = [&](std::uint64_t position) {
        if (position < replaced.first) return position;
        if (position >= replaced.last) return position - replaced.last + replaced.first + inserted;
        return std::uint64_t(replaced.first);
    };
    const auto relocateEnd = [&](std::uint64_t position) {
        if (position <= replaced.first) return position;
        if (position >= replaced.last) return position - replaced.last + replaced.first + inserted;
        return std::uint64_t(replaced.first + inserted);
    };
    const auto start = std::min<std::uint64_t>(relocateStart(sample.loopStart), result.size());
    const auto end = std::clamp<std::uint64_t>(relocateEnd(std::uint64_t(sample.loopStart) + sample.loopLength), start, result.size());
    sample.pcm = std::move(result);
    sample.loopStart = static_cast<std::uint32_t>(start);
    sample.loopLength = static_cast<std::uint32_t>(end - start);
    if (!sample.loopLength) sample.loopType = 0;
    if (!clip.pcm.empty() && clip.sixteenBit) sample.sixteenBit = true;
    return inserted;
}

void deletePcm(Sample& sample, FrameRange range) {
    checkRange(sample, range);
    pastePcm(sample, range, {{}, sample.sixteenBit});
}

void trimPcm(Sample& sample, FrameRange range) {
    checkRange(sample, range);
    auto result = copyPcm(sample, range);
    const auto end = std::uint64_t(sample.loopStart) + sample.loopLength;
    if (sample.loopStart >= range.first && end <= range.last) sample.loopStart -= static_cast<std::uint32_t>(range.first);
    else { sample.loopType = 0; sample.loopStart = sample.loopLength = 0; }
    sample.pcm = std::move(result.pcm);
}

void convertSampleDepth(Sample& sample, bool sixteenBit) {
    if (!sixteenBit)
        for (auto& value : sample.pcm) value = quantize(value, false);
    sample.sixteenBit = sixteenBit;
    if (!sixteenBit && sample.reserved == 0xAD) sample.reserved = 0;
}

void setSampleLoop(Sample& sample, std::uint8_t type, std::uint32_t first, std::uint32_t length) {
    require(type <= 3, "Invalid sample loop type");
    require(first <= sample.pcm.size() && std::uint64_t(first) + length <= sample.pcm.size(), "Loop lies outside PCM data");
    require(type == 0 || length > 0, "An enabled sample loop must contain at least one frame");
    sample.loopType = type; sample.loopStart = first; sample.loopLength = length;
}

void setEnvelopePoint(Envelope& envelope, std::size_t index, EnvPoint point) {
    checkPoint(envelope, index, point, false);
    envelope.points[index] = point;
}
void insertEnvelopePoint(Envelope& envelope, std::size_t index, EnvPoint point) {
    require(envelope.count < 12, "Envelope already has 12 points");
    checkPoint(envelope, index, point, true);
    std::move_backward(envelope.points.begin() + index, envelope.points.begin() + envelope.count,
                       envelope.points.begin() + envelope.count + 1);
    envelope.points[index] = point;
    if (envelope.count) {
        for (auto* marker : {&envelope.sustain, &envelope.loopStart, &envelope.loopEnd})
            if (*marker >= index) ++*marker;
    }
    ++envelope.count;
    repairMarkers(envelope);
}
void deleteEnvelopePoint(Envelope& envelope, std::size_t index) {
    require(envelope.count <= 12 && index < envelope.count, "Envelope point index is invalid");
    std::move(envelope.points.begin() + index + 1, envelope.points.begin() + envelope.count, envelope.points.begin() + index);
    --envelope.count;
    envelope.points[envelope.count] = {};
    for (auto* marker : {&envelope.sustain, &envelope.loopStart, &envelope.loopEnd})
        if (*marker > index) --*marker;
    repairMarkers(envelope);
}
void setEnvelopeFlags(Envelope& envelope, std::uint8_t flags) {
    require((flags & ~7) == 0 && envelope.count <= 12, "Invalid envelope flags or point count");
    require(flags == 0 || envelope.count > 0, "Add an envelope point before enabling its flags");
    repairMarkers(envelope);
    envelope.flags = flags;
}
void setEnvelopeMarkers(Envelope& envelope, std::uint8_t sustain, std::uint8_t loopStart, std::uint8_t loopEnd) {
    require(envelope.count >= 1 && envelope.count <= 12, "Envelope has no valid points");
    require(sustain < envelope.count && loopStart <= loopEnd && loopEnd < envelope.count, "Envelope markers are outside the point list");
    envelope.sustain = sustain; envelope.loopStart = loopStart; envelope.loopEnd = loopEnd;
}

std::uint8_t selectedFields(BlockRange range, std::size_t channel) {
    if (channel < range.firstChannel || channel >= range.lastChannel ||
        range.firstField >= 5 || range.lastField < 1 || range.lastField > 5) return 0;
    const auto first = channel == range.firstChannel ? range.firstField : 0;
    const auto last = channel == range.lastChannel - 1 ? range.lastField : 5;
    if (first >= last) return 0;
    return static_cast<std::uint8_t>(((1u << last) - 1) & ~((1u << first) - 1));
}

PatternBlock copyBlock(const Pattern& pattern, std::size_t channels, BlockRange range) {
    checkBlock(pattern, channels, range);
    PatternBlock result;
    result.rows = range.lastRow - range.firstRow; result.channels = range.lastChannel - range.firstChannel;
    result.cells.reserve(result.rows * result.channels);
    for (std::size_t row = range.firstRow; row < range.lastRow; ++row)
        for (std::size_t channel = range.firstChannel; channel < range.lastChannel; ++channel)
            result.cells.push_back(pattern.at(row, channel, channels));
    if (range.firstField != 0 || range.lastField != 5) {
        result.fieldMasks.reserve(result.channels);
        for (std::size_t channel = range.firstChannel; channel < range.lastChannel; ++channel)
            result.fieldMasks.push_back(selectedFields(range, channel));
    }
    return result;
}
void pasteBlock(Pattern& pattern, std::size_t channels, std::size_t row, std::size_t channel, const PatternBlock& block) {
    checkPattern(pattern, channels);
    require(block.rows > 0 && block.rows <= 256 && block.channels > 0 && block.channels <= 32 &&
            block.cells.size() == block.rows * block.channels, "Pattern clipboard dimensions are invalid");
    require(row < pattern.rows && block.rows <= pattern.rows - row && channel < channels && block.channels <= channels - channel,
            "Pattern clipboard does not fit; extend the pattern or move the cursor");
    require(block.fieldMasks.empty() || block.fieldMasks.size() == block.channels,
            "Pattern clipboard field masks do not match its channels");
    for (const auto fields : block.fieldMasks)
        require((fields & ~31u) == 0, "Pattern clipboard field mask is invalid");
    for (std::size_t r = 0; r < block.rows; ++r)
        for (std::size_t c = 0; c < block.channels; ++c)
            copyFields(pattern.at(row + r, channel + c, channels), block.cells[r * block.channels + c],
                       block.fieldMasks.empty() ? 31 : block.fieldMasks[c]);
}
void clearBlock(Pattern& pattern, std::size_t channels, BlockRange range) {
    checkBlock(pattern, channels, range);
    for (std::size_t row = range.firstRow; row < range.lastRow; ++row)
        for (std::size_t channel = range.firstChannel; channel < range.lastChannel; ++channel)
            copyFields(pattern.at(row, channel, channels), {}, selectedFields(range, channel));
}
void transposeBlock(Pattern& pattern, std::size_t channels, BlockRange range, int semitones) {
    checkBlock(pattern, channels, range);
    for (std::size_t row = range.firstRow; row < range.lastRow; ++row)
        for (std::size_t channel = range.firstChannel; channel < range.lastChannel; ++channel) {
            if (!(selectedFields(range, channel) & 1)) continue;
            const auto note = pattern.at(row, channel, channels).note;
            if (note >= 1 && note <= 96) {
                const auto moved = std::int64_t(note) + semitones;
                require(moved >= 1 && moved <= 96, "Transpose would move a note outside XM's 96-note range");
            }
        }
    for (std::size_t row = range.firstRow; row < range.lastRow; ++row)
        for (std::size_t channel = range.firstChannel; channel < range.lastChannel; ++channel) {
            if (!(selectedFields(range, channel) & 1)) continue;
            auto& note = pattern.at(row, channel, channels).note;
            if (note >= 1 && note <= 96) note = static_cast<std::uint8_t>(std::int64_t(note) + semitones);
        }
}
void adjustBlock(Pattern& pattern, std::size_t channels, BlockRange range, int direction, bool coarse,
                 std::size_t field, std::uint8_t cursorVolume) {
    checkBlock(pattern, channels, range);
    require(direction == -1 || direction == 1, "Pattern adjustment direction must be -1 or 1");
    require(field < 5, "Pattern adjustment field must be 0..4");
    const int noteDelta = direction * (coarse ? 12 : 1);
    const int valueDelta = direction * (coarse ? 16 : 1);
    // Validate every selected note before any field is changed, including fields
    // earlier than a rejected note in another channel or row.
    for (std::size_t row = range.firstRow; row < range.lastRow; ++row)
        for (std::size_t channel = range.firstChannel; channel < range.lastChannel; ++channel) {
            if (field != 0 || !(selectedFields(range, channel) & 1)) continue;
            const int note = pattern.at(row, channel, channels).note;
            if (note >= 1 && note <= 96)
                require(note + noteDelta >= 1 && note + noteDelta <= 96,
                        "Transpose would move a note outside XM's 96-note range");
        }
    for (std::size_t row = range.firstRow; row < range.lastRow; ++row)
        for (std::size_t channel = range.firstChannel; channel < range.lastChannel; ++channel) {
            const auto fields = selectedFields(range, channel) & (1u << field);
            auto& cell = pattern.at(row, channel, channels);
            const bool hasEffect = cell.effect != 0 || cell.param != 0;
            if ((fields & 1) && cell.note >= 1 && cell.note <= 96)
                cell.note = static_cast<std::uint8_t>(int(cell.note) + noteDelta);
            if ((fields & 2) && cell.instrument)
                cell.instrument = static_cast<std::uint8_t>(std::clamp(int(cell.instrument) + valueDelta, 1, 128));
            if (fields & 4) {
                if (cursorVolume < 0x60 && cell.volume >= 0x10 && cell.volume <= 0x50)
                    cell.volume = static_cast<std::uint8_t>(std::clamp(int(cell.volume) + valueDelta, 0x10, 0x50));
                else if (cursorVolume >= 0x60 && cell.volume >= 0x60) {
                    const int command = cell.volume >> 4, amount = cell.volume & 15;
                    cell.volume = static_cast<std::uint8_t>(coarse ?
                        (std::clamp(command + direction, 6, 15) << 4) | amount :
                        (command << 4) | std::clamp(amount + direction, 0, 15));
                }
            }
            if ((fields & 8) && hasEffect)
                cell.effect = static_cast<std::uint8_t>(std::clamp(int(cell.effect) + direction, 0, 35));
            if ((fields & 16) && hasEffect)
                cell.param = static_cast<std::uint8_t>(std::clamp(int(cell.param) + valueDelta, 0, 255));
        }
}
void insertRow(Pattern& pattern, std::size_t channels, std::size_t row,
               std::size_t firstChannel, std::size_t lastChannel, bool allowDiscard) {
    checkBlock(pattern, channels, {row, row + 1, firstChannel, lastChannel});
    if (!allowDiscard)
        for (std::size_t c = firstChannel; c < lastChannel; ++c)
            require(blank(pattern.at(pattern.rows - 1, c, channels)), "Insert would discard nonempty last-row cells");
    for (std::size_t r = pattern.rows - 1; r > row; --r)
        for (std::size_t c = firstChannel; c < lastChannel; ++c) pattern.at(r, c, channels) = pattern.at(r - 1, c, channels);
    for (std::size_t c = firstChannel; c < lastChannel; ++c) pattern.at(row, c, channels) = {};
}
void deleteRow(Pattern& pattern, std::size_t channels, std::size_t row,
               std::size_t firstChannel, std::size_t lastChannel) {
    checkBlock(pattern, channels, {row, row + 1, firstChannel, lastChannel});
    for (std::size_t r = row; r + 1 < pattern.rows; ++r)
        for (std::size_t c = firstChannel; c < lastChannel; ++c) pattern.at(r, c, channels) = pattern.at(r + 1, c, channels);
    for (std::size_t c = firstChannel; c < lastChannel; ++c) pattern.at(pattern.rows - 1, c, channels) = {};
}
void resizePattern(Pattern& pattern, std::size_t channels, std::uint16_t rows, bool allowDiscard) {
    checkPattern(pattern, channels);
    require(rows >= 1 && rows <= 256, "Pattern length must be 1..256");
    if (rows < pattern.rows && !allowDiscard)
        for (std::size_t i = std::size_t(rows) * channels; i < pattern.cells.size(); ++i)
            require(blank(pattern.cells[i]), "Shortening the pattern would discard nonempty rows");
    pattern.cells.resize(std::size_t(rows) * channels);
    pattern.rows = rows;
}
void resizeChannels(Document& document, std::uint16_t channels, bool allowDiscard) {
    require(channels >= 1 && channels <= 32, "Channel count must be 1..32");
    require(document.channels >= 1 && document.channels <= 32, "Existing channel count is invalid");
    for (const auto& pattern : document.patterns) {
        checkPattern(pattern, document.channels);
        if (channels < document.channels && !allowDiscard)
            for (std::size_t row = 0; row < pattern.rows; ++row)
                for (std::size_t channel = channels; channel < document.channels; ++channel)
                    require(blank(pattern.at(row, channel, document.channels)), "Removing channels would discard nonempty cells");
    }
    std::vector<Pattern> replacement;
    replacement.reserve(document.patterns.size());
    for (const auto& pattern : document.patterns) {
        Pattern next;
        next.rows = pattern.rows; next.cells.resize(std::size_t(next.rows) * channels);
        for (std::size_t row = 0; row < next.rows; ++row)
            for (std::size_t channel = 0; channel < std::min(channels, document.channels); ++channel)
                next.at(row, channel, channels) = pattern.at(row, channel, document.channels);
        replacement.push_back(std::move(next));
    }
    document.patterns = std::move(replacement); document.channels = channels;
}

struct DocumentDelta::Data {
    Header before, after;
    std::vector<PatternDelta> patterns;
    std::vector<InstrumentDelta> instruments;
    std::size_t storage = 0;
    Data(const Document& a, const Document& b) : before(a), after(b) {}
    void apply(Document& document, bool forward) const;
};

DocumentDelta DocumentDelta::between(const Document& before, const Document& after) {
    auto data = std::make_shared<Data>(before, after);
    data->storage = sizeof(Data) + before.name.size() + after.name.size() + before.orders.size() + after.orders.size();
    for (std::size_t i = 0; i < std::max(before.patterns.size(), after.patterns.size()); ++i) {
        if (i < before.patterns.size() && i < after.patterns.size() && same(before.patterns[i], after.patterns[i])) continue;
        PatternDelta change{i, {}, {}};
        if (i < before.patterns.size()) change.before = before.patterns[i];
        if (i < after.patterns.size()) change.after = after.patterns[i];
        data->storage += sizeof(PatternDelta) + (change.before.cells.size() + change.after.cells.size()) * sizeof(Cell);
        data->patterns.push_back(std::move(change));
    }
    const Instrument absent;
    const std::vector<std::int16_t> emptyPcm;
    for (std::size_t i = 0; i < std::max(before.instruments.size(), after.instruments.size()); ++i) {
        const auto& a = i < before.instruments.size() ? before.instruments[i] : absent;
        const auto& b = i < after.instruments.size() ? after.instruments[i] : absent;
        InstrumentDelta change{i, {}, {}, {}};
        for (std::size_t s = 0; s < std::max(a.samples.size(), b.samples.size()); ++s) {
            const auto& pa = s < a.samples.size() ? a.samples[s].pcm : emptyPcm;
            const auto& pb = s < b.samples.size() ? b.samples[s].pcm : emptyPcm;
            std::size_t first = 0, suffix = 0;
            while (first < std::min(pa.size(), pb.size()) && pa[first] == pb[first]) ++first;
            if (first == pa.size() && first == pb.size()) continue;
            while (suffix < pa.size() - first && suffix < pb.size() - first &&
                   pa[pa.size() - 1 - suffix] == pb[pb.size() - 1 - suffix]) ++suffix;
            PcmDelta pcm{s, first, pa.size(), pb.size(),
                         {pa.begin() + first, pa.end() - suffix}, {pb.begin() + first, pb.end() - suffix}};
            data->storage += sizeof(PcmDelta) + (pcm.before.size() + pcm.after.size()) * sizeof(std::int16_t);
            change.pcm.push_back(std::move(pcm));
        }
        if (change.pcm.empty() && sameInstrumentMetadata(a, b) && i < std::min(before.instruments.size(), after.instruments.size())) continue;
        change.before = instrumentMetadata(a); change.after = instrumentMetadata(b);
        data->storage += sizeof(InstrumentDelta) + a.name.size() + b.name.size() + (a.samples.size() + b.samples.size()) * sizeof(Sample);
        for (const auto& s : a.samples) data->storage += s.name.size();
        for (const auto& s : b.samples) data->storage += s.name.size();
        data->instruments.push_back(std::move(change));
    }
    DocumentDelta result;
    if (!data->before.matches(after) || !data->patterns.empty() || !data->instruments.empty()) result.data_ = std::move(data);
    return result;
}

void DocumentDelta::Data::apply(Document& document, bool forward) const {
    const auto& source = forward ? before : after;
    const auto& target = forward ? after : before;
    require(source.matches(document), "Undo history no longer matches document structure");
    Header nextHeader = target;
    std::vector<std::pair<std::size_t, Pattern>> nextPatterns;
    for (const auto& change : patterns) {
        const auto& expected = forward ? change.before : change.after;
        if (change.index < source.patterns) require(same(document.patterns[change.index], expected), "Undo pattern was changed outside history");
        if (change.index < target.patterns) nextPatterns.emplace_back(change.index, forward ? change.after : change.before);
    }
    struct Prepared { std::size_t index; Instrument instrument; std::vector<bool> replacedPcm; };
    std::vector<Prepared> nextInstruments;
    const Instrument absent;
    const std::vector<std::int16_t> emptyPcm;
    for (const auto& change : instruments) {
        const auto& current = change.index < document.instruments.size() ? document.instruments[change.index] : absent;
        const auto& expected = forward ? change.before : change.after;
        require(sameInstrumentMetadata(current, expected), "Undo instrument was changed outside history");
        const auto& metadata = forward ? change.after : change.before;
        Prepared next{change.index, metadata, std::vector<bool>(metadata.samples.size(), false)};
        for (const auto& span : change.pcm) {
            const auto& currentPcm = span.sampleIndex < current.samples.size() ? current.samples[span.sampleIndex].pcm : emptyPcm;
            const auto& oldMiddle = forward ? span.before : span.after;
            const auto& newMiddle = forward ? span.after : span.before;
            const auto sourceSize = forward ? span.beforeSize : span.afterSize;
            const auto targetSize = forward ? span.afterSize : span.beforeSize;
            require(currentPcm.size() == sourceSize && span.first <= sourceSize && oldMiddle.size() <= sourceSize - span.first,
                    "Undo sample length no longer matches history");
            require(std::equal(oldMiddle.begin(), oldMiddle.end(), currentPcm.begin() + span.first), "Undo sample PCM was changed outside history");
            if (span.sampleIndex >= next.instrument.samples.size()) continue;
            auto& pcm = next.instrument.samples[span.sampleIndex].pcm;
            pcm.reserve(targetSize);
            pcm.insert(pcm.end(), currentPcm.begin(), currentPcm.begin() + span.first);
            pcm.insert(pcm.end(), newMiddle.begin(), newMiddle.end());
            pcm.insert(pcm.end(), currentPcm.begin() + span.first + oldMiddle.size(), currentPcm.end());
            require(pcm.size() == targetSize, "Undo sample span is inconsistent");
            next.replacedPcm[span.sampleIndex] = true;
        }
        if (change.index < target.instruments) nextInstruments.push_back(std::move(next));
    }
    // Complete all potentially failing allocations before moving live PCM.
    document.patterns.reserve(target.patterns);
    document.instruments.reserve(target.instruments);
    if (document.patterns.size() < target.patterns) document.patterns.resize(target.patterns);
    if (document.instruments.size() < target.instruments) document.instruments.resize(target.instruments);
    for (auto& change : nextPatterns) document.patterns[change.first] = std::move(change.second);
    for (auto& next : nextInstruments) {
        auto& current = document.instruments[next.index];
        for (std::size_t s = 0; s < std::min(current.samples.size(), next.instrument.samples.size()); ++s)
            if (!next.replacedPcm[s]) next.instrument.samples[s].pcm = std::move(current.samples[s].pcm);
        current = std::move(next.instrument);
    }
    document.patterns.resize(target.patterns); document.instruments.resize(target.instruments);
    std::move(nextHeader).apply(document);
}
void DocumentDelta::undo(Document& document) const { if (data_) data_->apply(document, false); }
void DocumentDelta::redo(Document& document) const { if (data_) data_->apply(document, true); }
std::size_t DocumentDelta::bytes() const { return data_ ? data_->storage : 0; }
bool DocumentDelta::empty() const { return !data_; }

} // namespace brick::edit
