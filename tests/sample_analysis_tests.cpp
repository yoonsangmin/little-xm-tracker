#include "model/SampleAnalysis.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr double pi = 3.14159265358979323846, middleC = 261.6255653005986;
int checks = 0;
void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
double clockRate(const brick::Sample& sample) {
    return 8363.0 * std::exp2((sample.relativeNote + sample.finetune / 128.0) / 12.0);
}
brick::Sample sampleAtRate(double rate, double seconds) {
    brick::Sample sample;
    const int tuning = static_cast<int>(std::lround(1536.0 * std::log2(rate / 8363.0)));
    const int note = static_cast<int>(std::floor((tuning + 64.0) / 128.0));
    sample.relativeNote = static_cast<std::int8_t>(note);
    sample.finetune = static_cast<std::int8_t>(tuning - note * 128);
    sample.pcm.resize(static_cast<std::size_t>(rate * seconds));
    return sample;
}
brick::Sample tone(double frequency, double rate = 44100.0, double seconds = 0.5) {
    auto sample = sampleAtRate(rate, seconds);
    for (std::size_t i = 0; i < sample.pcm.size(); ++i)
        sample.pcm[i] = static_cast<std::int16_t>(std::lround(12000.0 * std::sin(2.0 * pi * frequency * i / rate)));
    return sample;
}
brick::edit::PitchEstimate detect(const brick::Sample& sample) {
    return brick::edit::detectSamplePitch(sample, {0, sample.pcm.size()});
}
void tonalPitches() {
    for (double rate : {8363.0, 22050.0, 44100.0, 48000.0, 96000.0, 384000.0}) {
        for (double frequency : {50.0, 55.0, 110.0, middleC, 440.0, 880.0, 1500.0, 2400.0}) {
            // Retuning a high-rate low tone up to C-4 can exceed FT2's playback
            // note range even though the original waveform itself is valid.
            if (rate * middleC / frequency > 383000.0) continue;
            const auto sample = tone(frequency, rate);
            const auto original = sample.pcm;
            const auto result = detect(sample);
            const auto label = "Tone " + std::to_string(frequency) + " Hz at " + std::to_string(rate) + " Hz: ";
            check(result.reliable, label + "must produce a reliable estimate");
            const double expected = frequency * clockRate(sample) / rate;
            check(std::abs(1200.0 * std::log2(result.frequency / expected)) < 10.0,
                  label + "detected frequency follows the current C-4 sample clock");
            const int expectedNote = 49 + static_cast<int>(std::lround(12.0 * std::log2(expected / middleC)));
            check(result.note == expectedNote && result.confidence >= 0.85 && result.confidence <= 1.0,
                  label + "note number and confidence describe a periodic single tone");
            const double corrected = expected * std::exp2((result.relativeNote + result.finetune / 128.0 -
                sample.relativeNote - sample.finetune / 128.0) / 12.0);
            check(std::abs(1200.0 * std::log2(corrected / middleC)) < 11.0 && result.finetune % 8 == 0,
                  label + "suggested tuning makes C-4 play musical C-4 at FT2 finetune precision");
            check(sample.pcm == original, label + "analysis must not resample or modify PCM");
        }
    }
    check(detect(tone(110.0, 1000.0)).reliable,
          "Imported clock rounding near the minimum supported file rate must remain analyzable");
    auto detuned = tone(440.0 * std::exp2(23.0 / 1200.0), 48000.0);
    const auto a = detect(detuned);
    check(a.reliable && a.note == 58 && std::abs(a.cents - 23.0) < 2.0,
          "A detuned A-4 reports signed cents instead of silently rounding the measured pitch");
    detuned.relativeNote += 12;
    const auto octave = detect(detuned);
    check(octave.reliable && octave.note == 70 && std::abs(octave.frequency / a.frequency - 2.0) < 0.003,
          "Existing sample tuning changes the detected C-4 playback frequency");
    check(octave.relativeNote == a.relativeNote && octave.finetune == a.finetune,
          "The same PCM needs the same final C-4 correction regardless of its previous relative note");
    auto high = tone(2400.0, 8363.0);
    const auto corrected = detect(high);
    check(corrected.reliable, "A high pitched low-rate sample can be corrected below the file import minimum rate");
    high.relativeNote = static_cast<std::int8_t>(corrected.relativeNote);
    high.finetune = static_cast<std::int8_t>(corrected.finetune);
    const auto repeated = detect(high);
    check(repeated.reliable && repeated.note == 49 && std::abs(repeated.cents) < 6.0,
          "Detecting again after applying the suggested tuning still works and reports C-4");
}

void harmonicsAndSelection() {
    auto harmonic = sampleAtRate(44100.0, 0.5);
    for (std::size_t i = 0; i < harmonic.pcm.size(); ++i) {
        const double phase = 2.0 * pi * 220.0 * i / 44100.0;
        harmonic.pcm[i] = static_cast<std::int16_t>(10000.0 *
            (0.35 * std::sin(phase) + 0.8 * std::sin(2.0 * phase) + 0.15 * std::sin(3.0 * phase)));
    }
    const auto estimated = detect(harmonic);
    check(estimated.reliable && std::abs(estimated.frequency - 220.0) < 1.0,
          "A stronger second harmonic must not force an octave error when the fundamental is present");

    auto changing = tone(220.0, 44100.0, 1.0);
    const auto second = tone(440.0, 44100.0, 0.5);
    std::copy(second.pcm.begin(), second.pcm.end(), changing.pcm.begin() + 22050);
    check(!detect(changing).reliable, "Different stable notes in the same range are not one reliable sample pitch");
    const auto firstPitch = brick::edit::detectSamplePitch(changing, {0, 22050});
    const auto secondPitch = brick::edit::detectSamplePitch(changing, {22050, changing.pcm.size()});
    check(firstPitch.reliable && secondPitch.reliable && firstPitch.note == 46 && secondPitch.note == 58,
          "Selecting each stable portion identifies that portion without analyzing outside PCM");
    auto quiet = tone(330.0);
    for (auto& value : quiet.pcm) value = static_cast<std::int16_t>(value / 20 + 10000);
    check(detect(quiet).reliable, "A quiet pitched sample with a large DC offset remains detectable");
    auto cycle = tone(8363.0 / 32.0, 8363.0, 32.0 / 8363.0);
    cycle.loopType = 1; cycle.loopLength = 32;
    const auto loopPitch = detect(cycle);
    check(loopPitch.reliable && loopPitch.note == 49 && std::abs(loopPitch.frequency - 8363.0 / 32.0) < 0.2,
          "An exact enabled forward loop can supply repeated evidence for a 32-frame tracker waveform");
    cycle.loopType = 0;
    check(!detect(cycle).reliable, "A short one-shot is not silently treated as a repeating waveform");
    cycle.loopType = 2;
    check(!detect(cycle).reliable, "A short ping-pong loop is not incorrectly analyzed as a forward repeat");
    cycle.loopType = 1;
    check(!brick::edit::detectSamplePitch(cycle, {0, 24}).reliable,
          "A partial loop selection is not expanded beyond its requested PCM range");
}

void unreliableInputs() {
    auto sample = sampleAtRate(44100.0, 0.5);
    check(!detect(sample).reliable, "Silence must not invent a pitch");
    std::fill(sample.pcm.begin(), sample.pcm.end(), std::int16_t{12000});
    check(!detect(sample).reliable, "DC alone must not invent a pitch");
    std::uint32_t random = 0x12345678u;
    for (auto& value : sample.pcm) {
        random = random * 1664525u + 1013904223u;
        value = static_cast<std::int16_t>(static_cast<int>(random >> 16) - 32768);
    }
    check(!detect(sample).reliable, "Broadband noise must remain unreliable");
    for (std::size_t i = 0; i < sample.pcm.size(); ++i) {
        const double time = static_cast<double>(i) / 44100.0;
        sample.pcm[i] = static_cast<std::int16_t>(6000.0 *
            (std::sin(2.0 * pi * 220.0 * time) + std::sin(2.0 * pi * 329.6276 * time)));
    }
    check(!detect(sample).reliable, "An unrelated pair of pitches must not be presented as a reliable single note");
    for (std::size_t i = 0; i < sample.pcm.size(); ++i) {
        const double time = static_cast<double>(i) / 44100.0;
        sample.pcm[i] = static_cast<std::int16_t>(6000.0 *
            (std::sin(2.0 * pi * 440.0 * time) + std::sin(2.0 * pi * 660.0 * time)));
    }
    check(!detect(sample).reliable, "A repeating pair with no audible common fundamental must remain ambiguous");
    for (std::size_t i = 0; i < sample.pcm.size(); ++i) {
        const double time = static_cast<double>(i) / 44100.0;
        sample.pcm[i] = static_cast<std::int16_t>(12000.0 * std::sin(2.0 * pi * (220.0 * time + 440.0 * time * time)));
    }
    check(!detect(sample).reliable, "A moving pitch must not be accepted from one convenient analysis window");
    check(!detect(tone(40.0)).reliable && !detect(tone(3500.0)).reliable,
          "Sounds outside the supported range must not be mislabeled by octave wrapping");
    check(!detect(tone(16000.0)).reliable, "Decimation must not turn a high-frequency tone into a reliable aliased note");
    check(!detect(tone(440.0, 44100.0, 0.001)).reliable, "Too little PCM must remain unreliable");
    sample = tone(55.0, 384000.0);
    check(!detect(sample).reliable, "A correction beyond FT2's playable relative-note range must not be offered");
    sample = tone(55.0, 96000.0);
    check(!detect(sample).reliable,
          "A correction above the app's 384 kHz PCM-preview clock limit must not be offered");
    sample.relativeNote = 127;
    check(!detect(sample).reliable, "Extreme sample clock metadata must not cause unbounded analysis work");
    bool rejected = false;
    try { brick::edit::detectSamplePitch(sample, {1, 1}); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "Empty selections are rejected explicitly");
    rejected = false;
    try { brick::edit::detectSamplePitch(sample, {0, sample.pcm.size() + 1}); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "Out-of-bounds selections are rejected before reading PCM");
}

void longSelection() {
    auto sample = sampleAtRate(48000.0, 60.0);
    for (std::size_t i = 0; i < sample.pcm.size(); ++i)
        sample.pcm[i] = static_cast<std::int16_t>(12000.0 * std::sin(2.0 * pi * 440.0 * i / 48000.0));
    const auto result = detect(sample);
    check(result.reliable && result.note == 58, "A minute-long sample uses the same bounded window analysis and detects its tone");
    // The endpoint is deliberately sampled, so length limiting cannot simply
    // analyze the beginning and ignore conflicting evidence near the end.
    for (std::size_t i = sample.pcm.size() - 48000; i < sample.pcm.size(); ++i)
        sample.pcm[i] = static_cast<std::int16_t>(12000.0 * std::sin(2.0 * pi * 660.0 * i / 48000.0));
    check(!detect(sample).reliable, "Bounded analysis also checks the end of a long selection for conflicting pitch");
}
} // namespace

int main() {
    try {
        tonalPitches(); harmonicsAndSelection(); unreliableInputs(); longSelection();
        std::cout << "Sample pitch analysis passed: " << checks << " checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
