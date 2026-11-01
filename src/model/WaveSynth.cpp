#include "model/WaveSynth.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace brick::edit {
namespace {
constexpr double pi = 3.1415926535897932384626433832795;
constexpr double peak = 16384.0;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void validate(const WaveSynthOptions& o) {
    require(o.preset >= WavePreset::Sine && o.preset <= WavePreset::Noise, "Unknown wave preset");
    require(o.noise == NoisePreset::Short || o.noise == NoisePreset::Long, "Unknown noise preset");
    require(o.frames >= 16 && o.frames <= 256 && (o.frames & (o.frames - 1)) == 0,
            "Tone frames must be 16, 32, 64, 128 or 256");
    require(o.dutyEighths == 1 || o.dutyEighths == 2 || o.dutyEighths == 4 || o.dutyEighths == 6,
            "Pulse duty must be 1, 2, 4 or 6 eighths");
    require(o.levels == 16 || o.levels == 256, "Wave levels must be 16 or 256");
    require(o.distortion >= WaveDistortion::Off && o.distortion <= WaveDistortion::Wrap,
            "Unknown wave distortion");
    require(o.drivePercent >= 0 && o.drivePercent <= 800, "Drive must be 0..800 percent");
    require(o.limitPercent >= 1 && o.limitPercent <= 100, "Limit must be 1..100 percent");
    require(o.filter >= WaveFilter::Off && o.filter <= WaveFilter::AllPass, "Unknown wave filter");
    require(o.cutoffPercent >= 1 && o.cutoffPercent <= 99, "Cutoff must be 1..99 percent of Nyquist");
    require(o.resonanceTenths >= 1 && o.resonanceTenths <= 100, "Q must be 0.1..10.0");
    require(o.vshiftPercent >= -100 && o.vshiftPercent <= 100, "Vertical shift must be -100..100 percent");
    require(o.phase >= WavePhase::Pinch && o.phase <= WavePhase::ResyncRaw, "Unknown wave phase mode");
    require(o.phaseAmount >= 0 && o.phaseAmount <= 100, "Phase amount must be 0..100");
}

double shapeAt(const WaveSynthOptions& o, double phase) {
    phase -= std::floor(phase);
    switch (o.preset) {
    case WavePreset::Sine: return std::sin(phase * (2.0 * pi));
    case WavePreset::Triangle: return 1.0 - 4.0 * std::abs(phase - 0.5);
    case WavePreset::Square: return phase < double(o.dutyEighths) / 8.0 ? 1.0 : -1.0;
    case WavePreset::Saw: return phase * 2.0 - 1.0;
    case WavePreset::Noise: break; // Noise is built from its complete register sequence.
    }
    return 0;
}

double phasedShape(const WaveSynthOptions& o, double phase) {
    if (!o.phaseAmount) return shapeAt(o, phase);
    const double amount = double(o.phaseAmount) / 100.0;
    const double compression = 1.0 + 7.0 * amount;
    switch (o.phase) {
    case WavePhase::Pinch:
        return phase * compression < 1.0 ? shapeAt(o, phase * compression) : 0.0;
    case WavePhase::Warp: {
        // Move the half-cycle boundary left while keeping both endpoints fixed.
        const double split = 0.5 - 0.4375 * amount;
        return shapeAt(o, phase < split ? phase * 0.5 / split :
                       0.5 + (phase - split) * 0.5 / (1.0 - split));
    }
    case WavePhase::Resync:
    case WavePhase::ResyncRaw: return shapeAt(o, phase * compression);
    }
    return 0;
}

std::vector<double> sourceWave(const WaveSynthOptions& o) {
    if (o.preset == WavePreset::Noise) {
        Sample noise;
        noise.sixteenBit = false;
        noise.pcm.resize(noisePresetFrames(o.noise));
        fillNoisePreset(noise, {0, noise.pcm.size()}, o.noise);
        std::vector<double> values(noise.pcm.size());
        for (std::size_t n = 0; n < values.size(); ++n) values[n] = double(noise.pcm[n]) / peak;
        return values;
    }
    std::vector<double> values(o.frames);
    if (o.phase == WavePhase::Resync && o.phaseAmount) {
        // Eight-times sampling plus a 129-tap Blackman-windowed sinc. Circular
        // convolution treats the final PCM as the repeating period it is,
        // including noninteger sync ratios and their wrap discontinuity.
        constexpr int oversample = 8, radius = 64;
        constexpr double cutoff = 0.45 / oversample;
        std::vector<double> high(o.frames * oversample);
        for (std::size_t n = 0; n < high.size(); ++n)
            high[n] = phasedShape(o, double(n) / double(high.size()));
        std::array<double, radius * 2 + 1> kernel{};
        double sum = 0;
        for (int k = -radius; k <= radius; ++k) {
            const double sinc = k ? std::sin(2.0 * pi * cutoff * k) / (pi * k) : 2.0 * cutoff;
            const double window = 0.42 + 0.5 * std::cos(pi * k / radius) +
                                  0.08 * std::cos(2.0 * pi * k / radius);
            kernel[std::size_t(k + radius)] = sinc * window;
            sum += sinc * window;
        }
        const auto length = static_cast<int>(high.size());
        for (std::size_t n = 0; n < values.size(); ++n) {
            double value = 0;
            for (int k = -radius; k <= radius; ++k) {
                const int index = (static_cast<int>(n) * oversample + k + length) % length;
                value += high[std::size_t(index)] * kernel[std::size_t(k + radius)];
            }
            values[n] = value / sum;
        }
    } else {
        for (std::size_t n = 0; n < values.size(); ++n)
            values[n] = phasedShape(o, double(n) / double(values.size()));
    }
    return values;
}

struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
};

Biquad coefficients(const WaveSynthOptions& o) {
    // Standard RBJ biquad responses, using Q rather than an opaque resonance
    // scale. Formula reference: https://www.w3.org/TR/audio-eq-cookbook/
    const double w = pi * double(o.cutoffPercent) / 100.0;
    const double c = std::cos(w), alpha = std::sin(w) / (2.0 * double(o.resonanceTenths) / 10.0);
    const double a0 = 1.0 + alpha;
    Biquad f;
    f.a1 = -2.0 * c / a0;
    f.a2 = (1.0 - alpha) / a0;
    switch (o.filter) {
    case WaveFilter::LowPass:
        f.b0 = f.b2 = (1.0 - c) / (2.0 * a0); f.b1 = (1.0 - c) / a0; break;
    case WaveFilter::HighPass:
        f.b0 = f.b2 = (1.0 + c) / (2.0 * a0); f.b1 = -(1.0 + c) / a0; break;
    case WaveFilter::BandPass:
        // Constant peak gain; Q controls the width, avoiding a mandatory boost.
        f.b0 = alpha / a0; f.b1 = 0; f.b2 = -f.b0; break;
    case WaveFilter::AllPass:
        f.b0 = f.a2; f.b1 = f.a1; f.b2 = 1; break;
    case WaveFilter::Off: break;
    }
    return f;
}

struct Matrix2 { double a = 1, b = 0, c = 0, d = 1; };
Matrix2 multiply(const Matrix2& x, const Matrix2& y) {
    return {x.a * y.a + x.b * y.c, x.a * y.b + x.b * y.d,
            x.c * y.a + x.d * y.c, x.c * y.b + x.d * y.d};
}
Matrix2 power(Matrix2 value, std::size_t exponent) {
    Matrix2 result;
    while (exponent) {
        if (exponent & 1u) result = multiply(result, value);
        value = multiply(value, value);
        exponent >>= 1u;
    }
    return result;
}

void periodicFilter(std::vector<double>& values, const Biquad& f) {
    // Transposed direct-form II: s_next = A*s + B*x. A complete period maps
    // initial state s to A^N*s+d. Solve (I-A^N)*s=d, so the first PCM frame
    // has exactly the steady state of the previous loop, without warm-up tails.
    double z1 = 0, z2 = 0;
    for (double x : values) {
        const double y = f.b0 * x + z1;
        z1 = f.b1 * x - f.a1 * y + z2;
        z2 = f.b2 * x - f.a2 * y;
    }
    const Matrix2 a = power({-f.a1, 1, -f.a2, 0}, values.size());
    const double m00 = 1.0 - a.a, m01 = -a.b, m10 = -a.c, m11 = 1.0 - a.d;
    const double determinant = m00 * m11 - m01 * m10;
    require(std::isfinite(determinant) && std::abs(determinant) > 1e-14,
            "Wave filter state is not stable");
    const double initial1 = (z1 * m11 - m01 * z2) / determinant;
    const double initial2 = (m00 * z2 - m10 * z1) / determinant;
    z1 = initial1; z2 = initial2;
    for (double& x : values) {
        const double y = f.b0 * x + z1;
        z1 = f.b1 * x - f.a1 * y + z2;
        z2 = f.b2 * x - f.a2 * y;
        x = y;
    }
}

double positiveModulo(double value, double period) {
    const double remainder = std::fmod(value, period);
    return remainder < 0 ? remainder + period : remainder;
}
double distort(double value, WaveDistortion mode, double limit) {
    switch (mode) {
    case WaveDistortion::Off: return value;
    case WaveDistortion::Clip: return std::clamp(value, -limit, limit);
    case WaveDistortion::Fold:
        if (value >= -limit && value <= limit) return value;
        return limit - std::abs(positiveModulo(value + limit, 4.0 * limit) - 2.0 * limit);
    case WaveDistortion::Wrap:
        if (value >= -limit && value <= limit) return value;
        return positiveModulo(value + limit, 2.0 * limit) - limit;
    }
    return 0;
}
} // namespace

Sample synthesizeWave(const WaveSynthOptions& o) {
    validate(o);
    auto values = sourceWave(o);
    for (double& value : values) value *= double(o.drivePercent) / 100.0;
    if (o.filter != WaveFilter::Off) periodicFilter(values, coefficients(o));
    const double bias = double(o.vshiftPercent) / 100.0;
    const double limit = double(o.limitPercent) / 100.0;
    bool silent = true;
    for (double& value : values) {
        value = distort(value + bias, o.distortion, limit);
        require(std::isfinite(value), "Wave generation produced an invalid value");
        silent = silent && std::abs(value) < 1e-12;
    }
    Sample sample;
    sample.name = "DRAWN WAVE";
    sample.sixteenBit = false;
    sample.loopType = 1;
    sample.loopLength = static_cast<std::uint32_t>(values.size());
    sample.pcm.resize(values.size());
    for (std::size_t n = 0; n < values.size(); ++n) {
        double value = silent ? 0.0 : values[n];
        if (!silent && o.levels == 16 && o.preset != WavePreset::Noise) {
            const double levelRange = o.distortion == WaveDistortion::Off ? 1.0 : limit;
            value = (std::round((std::clamp(value / levelRange, -1.0, 1.0) + 1.0) * 7.5) /
                     7.5 - 1.0) * levelRange;
        }
        // This final safety clamp is distinct from the chosen shaping mode.
        sample.pcm[n] = static_cast<std::int16_t>(std::clamp(std::lround(
            std::clamp(value * peak, -32768.0, 32767.0) / 256.0), -128L, 127L) * 256);
    }
    return sample;
}

} // namespace brick::edit
