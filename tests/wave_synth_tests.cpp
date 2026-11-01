#include "model/WaveSynth.hpp"
#include "io/Codec.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
using namespace brick;
using namespace brick::edit;
constexpr double pi = 3.1415926535897932384626433832795;
int checks = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
template<class Fn> void reject(Fn fn, const char* message) {
    bool failed = false;
    try { fn(); } catch (const std::exception&) { failed = true; }
    check(failed, message);
}
bool near(double value, double expected, double tolerance) { return std::abs(value - expected) <= tolerance; }
std::complex<double> harmonic(const Sample& sample, int index) {
    std::complex<double> value{};
    for (std::size_t n = 0; n < sample.pcm.size(); ++n)
        value += double(sample.pcm[n]) / 16384.0 *
                 std::polar(1.0, -2.0 * pi * index * double(n) / double(sample.pcm.size()));
    return value * 2.0 / double(sample.pcm.size());
}
double mean(const Sample& sample) {
    double value = 0;
    for (auto n : sample.pcm) value += n;
    return value / double(sample.pcm.size());
}
void checkPcm(const Sample& sample) {
    check(!sample.sixteenBit && sample.loopType == 1 && !sample.loopStart &&
          sample.loopLength == sample.pcm.size() && sample.name == "DRAWN WAVE",
          "Generated sample has ordinary XM PCM metadata and a full forward loop");
    check(std::all_of(sample.pcm.begin(), sample.pcm.end(), [](auto n) { return n % 256 == 0; }),
          "All generated PCM is canonical eight-bit data");
}
void neutralPresets() {
    for (const auto shape : {WavePreset::Sine, WavePreset::Triangle, WavePreset::Square, WavePreset::Saw})
        for (const int frames : {16, 32, 64, 128, 256})
            for (const int levels : {16, 256})
                for (const int duty : {1, 2, 4, 6}) {
                    WaveSynthOptions o;
                    o.preset = shape; o.frames = frames; o.levels = levels; o.dutyEighths = duty;
                    const auto generated = synthesizeWave(o);
                    Sample previous; previous.sixteenBit = false; previous.pcm.resize(frames);
                    fillWavePreset(previous, {0, previous.pcm.size()}, shape, 16384, 0x6d2b79f5u, duty, levels);
                    check(generated.pcm == previous.pcm, "Neutral synth retains every original tone preset byte-for-byte");
                    checkPcm(generated);
                    for (const auto phase : {WavePhase::Pinch, WavePhase::Warp, WavePhase::Resync, WavePhase::ResyncRaw}) {
                        o.phase = phase;
                        check(synthesizeWave(o).pcm == generated.pcm, "Every phase mode is exactly neutral at amount zero");
                    }
                }
    for (const auto type : {NoisePreset::Short, NoisePreset::Long}) {
        WaveSynthOptions o; o.preset = WavePreset::Noise; o.noise = type;
        const auto generated = synthesizeWave(o);
        Sample previous; previous.sixteenBit = false; previous.pcm.resize(noisePresetFrames(type));
        fillNoisePreset(previous, {0, previous.pcm.size()}, type);
        check(generated.pcm == previous.pcm, "Neutral noise keeps the same full deterministic LFSR sequence");
        checkPcm(generated);
        o.phaseAmount = 100; o.phase = WavePhase::Resync; o.levels = 16;
        check(synthesizeWave(o).pcm == generated.pcm, "Noise ignores tone-only phase and level controls");
    }
}

void distortionControls() {
    WaveSynthOptions o; o.frames = 32; o.drivePercent = 200;
    o.distortion = WaveDistortion::Clip;
    const auto clipped = synthesizeWave(o);
    o.distortion = WaveDistortion::Fold;
    const auto folded = synthesizeWave(o);
    o.distortion = WaveDistortion::Wrap;
    const auto wrapped = synthesizeWave(o);
    check(clipped.pcm[8] == 16384 && folded.pcm[8] == 0 && wrapped.pcm[8] == 0,
          "Clip bounds an overdriven sine peak while fold/wrap return the two-times peak to zero");
    check(near(folded.pcm[4], (2.0 - std::sqrt(2.0)) * 16384.0, 128) &&
          near(wrapped.pcm[4], (std::sqrt(2.0) - 2.0) * 16384.0, 128),
          "Fold reflects and wrap jumps to the opposite side after exceeding the threshold");
    check(clipped.pcm != folded.pcm && folded.pcm != wrapped.pcm,
          "All distortion modes produce distinguishable PCM");
    for (const auto mode : {WaveDistortion::Clip, WaveDistortion::Fold, WaveDistortion::Wrap})
        for (const int levels : {16, 256}) {
            o.distortion = mode; o.limitPercent = 30; o.levels = levels; o.drivePercent = 800;
            const auto limited = synthesizeWave(o);
            check(std::all_of(limited.pcm.begin(), limited.pcm.end(), [](auto n) { return std::abs(int(n)) <= 5043; }),
                  "LIMIT bounds distortion output even with reduced levels and maximum drive");
        }
    o = {}; o.drivePercent = 0; o.levels = 16;
    const auto silence = synthesizeWave(o);
    check(std::all_of(silence.pcm.begin(), silence.pcm.end(), [](auto n) { return n == 0; }),
          "Zero drive stays silent even with the zero-less 16-level amplitude alphabet");
    o = {}; o.distortion = WaveDistortion::Off;
    const auto normal = synthesizeWave(o);
    o.limitPercent = 1;
    check(synthesizeWave(o).pcm == normal.pcm, "LIMIT is inactive while DIST is OFF");
    o.drivePercent = 800;
    const auto saturated = synthesizeWave(o);
    check(*std::max_element(saturated.pcm.begin(), saturated.pcm.end()) == 32512 &&
          *std::min_element(saturated.pcm.begin(), saturated.pcm.end()) == -32768,
          "DIST OFF still saturates safely at the final canonical PCM boundary");
    o = {}; o.vshiftPercent = 25;
    check(near(mean(synthesizeWave(o)), 4096, 128), "VSHIFT changes vertical bias without shifting time");
    o = {}; o.preset = WavePreset::Square; o.dutyEighths = 1;
    const auto narrow = synthesizeWave(o);
    o.drivePercent = 300; o.distortion = WaveDistortion::Clip;
    check(synthesizeWave(o).pcm == narrow.pcm,
          "Drive and clipping do not silently substitute for the pulse duty control");
    const auto saved = synthesizeWave(o);
    o.distortion = WaveDistortion::Wrap; synthesizeWave(o);
    o.distortion = WaveDistortion::Clip;
    check(synthesizeWave(o).pcm == saved.pcm, "Regeneration always starts from the shape, without destructive accumulation");
}

void filtersAndPeriodicity() {
    WaveSynthOptions o; o.frames = 64; o.drivePercent = 25;
    const auto dry = synthesizeWave(o);
    o.filter = WaveFilter::AllPass; o.cutoffPercent = 3; o.resonanceTenths = 7;
    const auto allpass = synthesizeWave(o);
    const auto dryFundamental = harmonic(dry, 1), shiftedFundamental = harmonic(allpass, 1);
    check(std::abs(shiftedFundamental - dryFundamental) > 0.3 &&
          near(std::abs(shiftedFundamental), std::abs(dryFundamental), 0.004),
          "ALLPASS changes phase while retaining fundamental magnitude");
    o.drivePercent = 10; // Keep even the highest-Q response below PCM clipping.
    // Compare the entire sine cycle to the analytical transfer response. A
    // zero-state one-shot filter fails especially at the first loop frames.
    for (const auto type : {WaveFilter::LowPass, WaveFilter::HighPass, WaveFilter::BandPass, WaveFilter::AllPass})
        for (const int cutoff : {1, 3, 25, 99})
            for (const int q : {1, 7, 100}) {
                o.filter = type; o.cutoffPercent = cutoff; o.resonanceTenths = q;
                const auto filtered = synthesizeWave(o);
                const double w = pi * cutoff / 100.0, c = std::cos(w), alpha = std::sin(w) / (2.0 * q / 10.0);
                double b0 = 0, b1 = 0, b2 = 0;
                switch (type) {
                case WaveFilter::LowPass: b0 = b2 = (1 - c) / 2; b1 = 1 - c; break;
                case WaveFilter::HighPass: b0 = b2 = (1 + c) / 2; b1 = -1 - c; break;
                case WaveFilter::BandPass: b0 = alpha; b2 = -alpha; break;
                case WaveFilter::AllPass: b0 = 1 - alpha; b1 = -2 * c; b2 = 1 + alpha; break;
                case WaveFilter::Off: break;
                }
                const auto z = std::polar(1.0, -2.0 * pi / double(o.frames));
                const auto transfer = (b0 + b1 * z + b2 * z * z) /
                                      (1.0 + alpha - 2.0 * c * z + (1.0 - alpha) * z * z);
                for (std::size_t n = 0; n < filtered.pcm.size(); ++n) {
                    const double expected = std::imag(transfer * std::polar(0.10, 2.0 * pi * n / o.frames)) * 16384;
                    if (!near(filtered.pcm[n], expected, 128.001))
                        std::cerr << "Filter " << int(type) << " cutoff " << cutoff << " Q " << q << " frame " << n
                                  << " expected " << expected << " actual " << filtered.pcm[n] << '\n';
                    check(near(filtered.pcm[n], expected, 128.001),
                          "Every filtered PCM frame matches the steady periodic sine response, including the boundary");
                }
            }
    o = {}; o.preset = WavePreset::Saw; o.frames = 128; o.drivePercent = 25;
    const auto saw = synthesizeWave(o);
    o.filter = WaveFilter::LowPass; o.cutoffPercent = 4;
    const auto low = synthesizeWave(o);
    o.filter = WaveFilter::HighPass;
    const auto high = synthesizeWave(o);
    o.filter = WaveFilter::BandPass;
    const auto band = synthesizeWave(o);
    check(std::abs(harmonic(low, 12)) < std::abs(harmonic(saw, 12)) * 0.1 &&
          std::abs(harmonic(high, 1)) < std::abs(harmonic(saw, 1)) * 0.3 &&
          std::abs(harmonic(band, 12)) < std::abs(harmonic(saw, 12)) * 0.4,
          "Low/high/band-pass shaping removes the expected frequency regions");
    o.filter = WaveFilter::LowPass; o.resonanceTenths = 30;
    check(synthesizeWave(o).pcm != low.pcm, "Q changes the filter response");
}

void phaseModes() {
    WaveSynthOptions o; o.frames = 64; o.phaseAmount = 100;
    o.phase = WavePhase::Pinch;
    const auto pinched = synthesizeWave(o);
    check(std::all_of(pinched.pcm.begin() + 8, pinched.pcm.end(), [](auto n) { return n == 0; }) &&
          std::any_of(pinched.pcm.begin(), pinched.pcm.begin() + 8, [](auto n) { return n != 0; }),
          "PINCH compresses one cycle into the start and leaves the remainder at zero");
    o.preset = WavePreset::Square; o.phase = WavePhase::Warp;
    const auto warped = synthesizeWave(o);
    check(std::count(warped.pcm.begin(), warped.pcm.end(), 16384) == 4,
          "WARP moves the half-cycle boundary while keeping a continuous time span");
    o.preset = WavePreset::Sine; o.phase = WavePhase::ResyncRaw;
    const auto synced = synthesizeWave(o);
    check(std::abs(harmonic(synced, 8)) > 0.99 && std::abs(harmonic(synced, 1)) < 0.01,
          "RESYN2 repeats the compressed cycle at the intended sync ratio");
    o.frames = 16; o.preset = WavePreset::Saw; o.phaseAmount = 80;
    const auto raw = synthesizeWave(o);
    o.phase = WavePhase::Resync;
    const auto reduced = synthesizeWave(o);
    check(reduced.pcm != raw.pcm, "RESYNC performs actual oversampled filtering rather than duplicating RESYN2");
    // A near-Nyquist resynced saw has strong harmonics above target Nyquist.
    // Reference the fundamental of its 6.6-cycle source: fold-back energy at
    // low output bins must fall substantially after antialias reduction.
    double rawLow = 0, reducedLow = 0;
    for (int n = 1; n <= 4; ++n) {
        rawLow += std::norm(harmonic(raw, n));
        reducedLow += std::norm(harmonic(reduced, n));
    }
    check(reducedLow < rawLow * 0.6, "RESYNC reduces folded-back high-harmonic energy compared with RESYN2");
}

void combinationsAndStorage() {
    Instrument instrument; instrument.name = "STATIC SYNTH";
    for (const auto shape : {WavePreset::Sine, WavePreset::Triangle, WavePreset::Square, WavePreset::Saw, WavePreset::Noise})
        for (const auto filter : {WaveFilter::Off, WaveFilter::LowPass, WaveFilter::HighPass, WaveFilter::BandPass, WaveFilter::AllPass})
            for (const auto distortion : {WaveDistortion::Off, WaveDistortion::Clip, WaveDistortion::Fold, WaveDistortion::Wrap}) {
                WaveSynthOptions o;
                o.preset = shape; o.filter = filter; o.distortion = distortion;
                o.noise = NoisePreset::Short; o.frames = 16; o.drivePercent = 800;
                o.cutoffPercent = 1; o.resonanceTenths = 100; o.limitPercent = 1;
                o.vshiftPercent = -100; o.phaseAmount = 100; o.phase = WavePhase::Resync;
                const auto generated = synthesizeWave(o);
                checkPcm(generated);
                check(generated.pcm == synthesizeWave(o).pcm, "Extreme combinations remain deterministic and bounded");
                if (filter == WaveFilter::AllPass && distortion == WaveDistortion::Fold)
                    instrument.samples.push_back(generated);
            }
    WaveSynthOptions noise; noise.preset = WavePreset::Noise; noise.filter = WaveFilter::LowPass;
    noise.distortion = WaveDistortion::Fold; noise.drivePercent = 600; noise.resonanceTenths = 100;
    const auto longNoise = synthesizeWave(noise);
    check(longNoise.pcm.size() == 32767, "Processing LONG noise retains the complete loop length");
    checkPcm(longNoise); instrument.samples.push_back(longNoise);
    const auto xi = io::decodeXi(io::encodeXi(instrument));
    auto doc = makeDemoDocument(); doc.instruments = {instrument};
    const auto xm = io::decodeXm(io::encodeXm(doc));
    for (std::size_t n = 0; n < instrument.samples.size(); ++n) {
        check(xi.samples[n].pcm == instrument.samples[n].pcm &&
              xm.instruments[0].samples[n].pcm == instrument.samples[n].pcm &&
              xi.samples[n].loopLength == instrument.samples[n].loopLength,
              "Processed waveform PCM and full-period loop survive standard XI and XM storage");
    }
}

void invalidOptions() {
    WaveSynthOptions o;
    o.frames = 0; reject([&] { synthesizeWave(o); }, "Zero tone length rejected");
    o.frames = 33; reject([&] { synthesizeWave(o); }, "Unsupported tone length rejected");
    o.frames = std::size_t(-1); reject([&] { synthesizeWave(o); }, "Oversized request rejected before allocation");
    o = {}; o.preset = static_cast<WavePreset>(99); reject([&] { synthesizeWave(o); }, "Unknown shape rejected");
    o = {}; o.noise = static_cast<NoisePreset>(99); reject([&] { synthesizeWave(o); }, "Unknown noise type rejected");
    o = {}; o.distortion = static_cast<WaveDistortion>(99); reject([&] { synthesizeWave(o); }, "Unknown distortion rejected");
    o = {}; o.filter = static_cast<WaveFilter>(99); reject([&] { synthesizeWave(o); }, "Unknown filter rejected");
    o = {}; o.phase = static_cast<WavePhase>(99); reject([&] { synthesizeWave(o); }, "Unknown phase mode rejected");
    o = {}; o.drivePercent = -1; reject([&] { synthesizeWave(o); }, "Negative drive rejected");
    o = {}; o.drivePercent = 801; reject([&] { synthesizeWave(o); }, "Excessive drive rejected");
    o = {}; o.limitPercent = 0; reject([&] { synthesizeWave(o); }, "Zero distortion limit rejected");
    o = {}; o.limitPercent = 101; reject([&] { synthesizeWave(o); }, "Oversized distortion limit rejected");
    o = {}; o.cutoffPercent = 0; reject([&] { synthesizeWave(o); }, "Zero cutoff rejected");
    o = {}; o.cutoffPercent = 100; reject([&] { synthesizeWave(o); }, "Nyquist cutoff rejected");
    o = {}; o.resonanceTenths = 0; reject([&] { synthesizeWave(o); }, "Zero Q rejected");
    o = {}; o.resonanceTenths = 101; reject([&] { synthesizeWave(o); }, "Excessive Q rejected");
    o = {}; o.vshiftPercent = -101; reject([&] { synthesizeWave(o); }, "Out-of-range bias rejected");
    o = {}; o.phaseAmount = 101; reject([&] { synthesizeWave(o); }, "Out-of-range phase amount rejected");
    o = {}; o.levels = 17; reject([&] { synthesizeWave(o); }, "Unsupported quantizer rejected");
    o = {}; o.dutyEighths = 3; reject([&] { synthesizeWave(o); }, "Unsupported duty rejected");
}
} // namespace

int main() {
    try {
        neutralPresets(); distortionControls(); filtersAndPeriodicity(); phaseModes();
        combinationsAndStorage(); invalidOptions();
        std::cout << "Wave synth passed: " << checks << " checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
