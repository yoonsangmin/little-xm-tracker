#include "model/SampleAnalysis.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace brick::edit {
namespace {
constexpr double pi = 3.14159265358979323846;
constexpr double middleC = 261.6255653005986;
constexpr double minimumFrequency = 50.0, maximumFrequency = 2500.0;
constexpr std::size_t maximumWindow = 2048, maximumWindows = 5;

struct WindowPitch { double frequency = 0.0, confidence = 0.0; bool audible = false; };

// A short low-pass FIR precedes integer decimation. Merely skipping PCM frames
// can alias an ultrasonic/noisy sample into a very convincing low pitched tone.
std::vector<double> decimationFilter(std::size_t stride) {
    if (stride == 1) return {1.0};
    const int radius = static_cast<int>(stride * 8);
    const double cutoff = 0.42 / static_cast<double>(stride);
    std::vector<double> weights(static_cast<std::size_t>(radius * 2 + 1));
    double total = 0.0;
    for (int i = -radius; i <= radius; ++i) {
        const double sinc = i == 0 ? 2.0 * cutoff : std::sin(2.0 * pi * cutoff * i) / (pi * i);
        const double window = 0.5 + 0.5 * std::cos(pi * i / radius);
        const auto index = static_cast<std::size_t>(i + radius);
        weights[index] = sinc * window;
        total += weights[index];
    }
    for (auto& value : weights) value /= total;
    return weights;
}

WindowPitch analyzeWindow(const std::vector<double>& values, double rate, bool interpolate = true) {
    if (rate < minimumFrequency * 2.0) return {};
    if (interpolate && rate < 12000.0) {
        // A two-times band-limited interpolation avoids octave ambiguity from
        // only three/four source frames per period at the top of our range.
        std::array<double, 16> weights{};
        double total = 0.0;
        for (int tap = 0; tap < 16; ++tap) {
            const double distance = tap - 7.5;
            weights[static_cast<std::size_t>(tap)] = std::sin(pi * distance) / (pi * distance) *
                (0.5 + 0.5 * std::cos(pi * distance / 8.0));
            total += weights[static_cast<std::size_t>(tap)];
        }
        std::vector<double> doubled(values.size() * 2);
        for (std::size_t i = 0; i < values.size(); ++i) {
            doubled[i * 2] = values[i];
            double value = 0.0;
            for (int tap = 0; tap < 16; ++tap) {
                const auto source = std::clamp(static_cast<std::ptrdiff_t>(i) + tap - 7,
                    std::ptrdiff_t{0}, static_cast<std::ptrdiff_t>(values.size() - 1));
                value += values[static_cast<std::size_t>(source)] * weights[static_cast<std::size_t>(tap)] / total;
            }
            doubled[i * 2 + 1] = value;
        }
        return analyzeWindow(doubled, rate * 2.0, false);
    }
    WindowPitch result;
    double sum = 0.0, squares = 0.0;
    for (double value : values) { sum += value; squares += value * value; }
    const double mean = sum / values.size();
    const double energy = std::max(0.0, squares - sum * mean);
    if (energy / values.size() < 32.0 * 32.0) return result;
    result.audible = true;
    const auto maximumLag = std::min(static_cast<std::size_t>(std::ceil(rate / minimumFrequency)) + 2, values.size() / 3);
    const auto minimumLag = std::max<std::size_t>(2, static_cast<std::size_t>(std::floor(rate / maximumFrequency)));
    if (maximumLag <= minimumLag + 2) return result;
    const auto pairs = values.size() - maximumLag;
    std::vector<double> difference(maximumLag + 1), normalized(maximumLag + 1, 1.0);
    double cumulative = 0.0;
    for (std::size_t lag = 1; lag <= maximumLag; ++lag) {
        double distance = 0.0;
        for (std::size_t i = 0; i < pairs; ++i) {
            const double delta = values[i] - values[i + lag];
            distance += delta * delta;
        }
        difference[lag] = distance;
        cumulative += distance;
        if (cumulative > 0.0) normalized[lag] = distance * lag / cumulative;
    }
    const auto valley = [&](std::size_t lag) {
        const double a = normalized[lag - 1], b = normalized[lag], c = normalized[lag + 1];
        const double curve = a - 2.0 * b + c;
        return curve > 0.0 ? std::max(0.0, b - (a - c) * (a - c) / (8.0 * curve)) : b;
    };
    // YIN-style cumulative normalized difference: choose the first sufficiently
    // periodic local minimum, rather than the global minimum at a subharmonic.
    std::size_t candidate = 0;
    for (std::size_t lag = minimumLag; lag < maximumLag; ++lag) {
        if (valley(lag) < 0.15 && normalized[lag] <= normalized[lag - 1] &&
            normalized[lag] <= normalized[lag + 1]) { candidate = lag; break; }
    }
    if (!candidate) return result;
    // A loud second/third harmonic can create an earlier shallow minimum.
    // Prefer its fundamental only when the longer period is substantially better.
    const auto firstCandidate = candidate;
    for (std::size_t multiple = 2; multiple <= 3; ++multiple) {
        const auto center = firstCandidate * multiple;
        if (center + multiple >= maximumLag) break;
        auto better = center - multiple;
        for (auto lag = center - multiple; lag <= center + multiple; ++lag)
            if (normalized[lag] < normalized[better]) better = lag;
        if (valley(better) + 0.025 < valley(candidate) &&
            valley(better) < valley(candidate) * 0.35) candidate = better;
    }
    const auto refinedLag = [&](std::size_t lag) {
        const double left = difference[lag - 1], center = difference[lag], right = difference[lag + 1];
        const double curvature = left - 2.0 * center + right;
        const double offset = curvature > 0.0 ? std::clamp(0.5 * (left - right) / curvature, -0.5, 0.5) : 0.0;
        return static_cast<double>(lag) + offset;
    };
    double period = refinedLag(candidate);
    // Measure several periods together when possible. Parabolic interpolation
    // of a three/four-frame period alone otherwise has substantial pitch bias.
    const auto periods = std::min<std::size_t>(8, static_cast<std::size_t>((maximumLag - 2) / (period + 1.0)));
    if (periods >= 2) {
        const auto center = static_cast<std::size_t>(std::lround(period * periods));
        auto refined = center;
        for (auto lag = center - 1; lag <= center + 1; ++lag)
            if (difference[lag] < difference[refined]) refined = lag;
        if (valley(refined) < 0.10) period = refinedLag(refined) / periods;
    }
    const double frequency = rate / period;
    if (!std::isfinite(frequency) || frequency < minimumFrequency * 0.99 || frequency > maximumFrequency * 1.01) return result;

    // Require audible energy at the proposed fundamental. A chord can repeat
    // at a low common period without actually containing that musical note.
    // This deliberately rejects missing-fundamental sounds instead of guessing.
    double real = 0.0, imaginary = 0.0;
    for (std::size_t i = 0; i < values.size(); ++i) {
        const double phase = 2.0 * pi * frequency * static_cast<double>(i) / rate;
        real += (values[i] - mean) * std::cos(phase);
        imaginary += (values[i] - mean) * std::sin(phase);
    }
    const double fundamentalShare = 2.0 * (real * real + imaginary * imaginary) / (values.size() * energy);
    if (fundamentalShare < 0.06) return result;
    result.frequency = frequency;
    result.confidence = std::clamp(1.0 - valley(candidate), 0.0, 1.0);
    return result;
}
} // namespace

PitchEstimate detectSamplePitch(const Sample& sample, FrameRange range) {
    if (range.first >= range.last || range.last > sample.pcm.size())
        throw std::invalid_argument("Select a nonempty PCM range for pitch detection");
    PitchEstimate result;
    const double originalTuning = sample.relativeNote + sample.finetune / 128.0;
    const double sampleRate = 8363.0 * std::exp2(originalTuning / 12.0);
    // Cap FIR work at the app's sample-clock limit, with room for imported
    // 384 kHz metadata's 1/128-semitone rate rounding. Low clocks are legitimate
    // after tuning; insufficient bandwidth/evidence is rejected by the detector.
    if (!std::isfinite(sampleRate) || sampleRate < 1.0 || sampleRate > 385000.0) return result;
    const auto stride = std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(sampleRate / 16000.0)));
    const double analysisRate = sampleRate / static_cast<double>(stride);
    const auto length = range.last - range.first;
    const bool cyclic = sample.loopType == 1 && sample.loopStart == range.first && sample.loopLength == length;
    const auto available = cyclic ? std::max(length / stride, maximumWindow * 3) : length / stride;
    if (available < 256) return result;
    const auto windowLength = std::min(available, std::min(maximumWindow, std::max<std::size_t>(512, available / 3)));
    const auto windows = std::min(maximumWindows, available - windowLength + 1);
    const auto weights = decimationFilter(stride);
    const auto radius = static_cast<std::ptrdiff_t>(weights.size() / 2);
    std::vector<double> values(windowLength);
    std::array<double, maximumWindows> frequencies{};
    std::size_t accepted = 0, audible = 0;
    double confidence = 1.0;
    for (std::size_t window = 0; window < windows; ++window) {
        const auto start = windows == 1 ? 0 : (available - windowLength) * window / (windows - 1);
        for (std::size_t i = 0; i < windowLength; ++i) {
            const auto frame = static_cast<std::ptrdiff_t>(range.first + (start + i) * stride);
            double value = 0.0;
            for (std::size_t tap = 0; tap < weights.size(); ++tap) {
                auto source = frame + static_cast<std::ptrdiff_t>(tap) - radius;
                if (cyclic) {
                    auto offset = (source - static_cast<std::ptrdiff_t>(range.first)) % static_cast<std::ptrdiff_t>(length);
                    if (offset < 0) offset += static_cast<std::ptrdiff_t>(length);
                    source = static_cast<std::ptrdiff_t>(range.first) + offset;
                } else source = std::clamp(source,
                    static_cast<std::ptrdiff_t>(range.first), static_cast<std::ptrdiff_t>(range.last - 1));
                value += sample.pcm[static_cast<std::size_t>(source)] * weights[tap];
            }
            values[i] = value;
        }
        const auto detected = analyzeWindow(values, analysisRate);
        if (detected.audible) ++audible;
        if (detected.frequency > 0.0) {
            frequencies[accepted++] = detected.frequency;
            confidence = std::min(confidence, detected.confidence);
        }
    }
    // Every audible inspected window must agree; do not select one convenient
    // note from a noisy/variable recording. Short clips may have one window.
    if (!accepted || accepted != audible || (windows > 1 && accepted < 2)) return result;
    std::sort(frequencies.begin(), frequencies.begin() + static_cast<std::ptrdiff_t>(accepted));
    const double frequency = frequencies[accepted / 2];
    double largestDeviation = 0.0;
    for (std::size_t i = 0; i < accepted; ++i)
        largestDeviation = std::max(largestDeviation, std::abs(1200.0 * std::log2(frequencies[i] / frequency)));
    if (largestDeviation > 25.0) return result;

    const double semitones = 12.0 * std::log2(frequency / middleC);
    const int nearest = static_cast<int>(std::lround(semitones));
    const int note = 49 + nearest;
    if (note < 1 || note > 96) return result;
    // Pinned MilkyPlay's FT2 getperiod() quantizes finetune to multiples of 8
    // before either its linear or Amiga frequency calculation. Choose the
    // closest representable setting instead of a value it would round down.
    const int tuning = static_cast<int>(std::lround((originalTuning - semitones) * 16.0)) * 8;
    const int relative = static_cast<int>(std::floor((tuning + 64.0) / 128.0));
    const int fine = tuning - relative * 128;
    const double correctedRate = 8363.0 * std::exp2((relative + fine / 128.0) / 12.0);
    if (relative < -128 || relative > 127 || fine < -128 || fine > 127 ||
        49 + relative < 1 || 49 + relative > 119 || correctedRate < 1.0 || correctedRate > 384000.0) return result;
    result.reliable = true;
    result.frequency = frequency;
    result.confidence = std::clamp(confidence * (1.0 - largestDeviation / 250.0), 0.0, 1.0);
    result.cents = (semitones - nearest) * 100.0;
    result.note = note;
    result.relativeNote = relative;
    result.finetune = fine;
    return result;
}

} // namespace brick::edit
