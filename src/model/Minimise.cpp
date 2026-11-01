#include "model/Minimise.hpp"

#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace brick::edit {
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::size_t retainedFrames(const Sample& sample) {
    require(sample.loopType <= 3, "Minimise found an invalid sample loop type");
    if (!sample.loopType) return sample.pcm.size();
    // Convert before adding: two valid uint32 markers can overflow uint32.
    const auto end = std::uint64_t(sample.loopStart) + std::uint64_t(sample.loopLength);
    require(sample.loopLength > 0 && sample.loopStart < sample.pcm.size() && end <= sample.pcm.size(),
            "Minimise found an enabled loop outside its sample PCM");
    return static_cast<std::size_t>(end);
}

template<class Scope, class Fn> void forSamples(Scope& scope, Fn&& fn) {
    using Type = std::remove_cv_t<Scope>;
    if constexpr (std::is_same_v<Type, Sample>) fn(scope);
    else if constexpr (std::is_same_v<Type, Instrument>) {
        for (auto& sample : scope.samples) fn(sample);
    } else {
        for (auto& instrument : scope.instruments)
            for (auto& sample : instrument.samples) fn(sample);
    }
}

void addSavings(MinimiseSummary& total, const Sample& sample, std::size_t keep) {
    if (keep == sample.pcm.size()) return;
    const auto removed = std::uint64_t(sample.pcm.size() - keep);
    const std::uint64_t width = sample.sixteenBit ? 2 : 1;
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    require(removed <= maximum / width && total.framesRemoved <= maximum - removed &&
            total.bytesRemoved <= maximum - removed * width &&
            total.samplesChanged < std::numeric_limits<std::size_t>::max(),
            "Minimise savings exceed the supported range");
    ++total.samplesChanged;total.framesRemoved += removed;total.bytesRemoved += removed * width;
}

template<class Scope> MinimiseSummary analyze(const Scope& scope) {
    MinimiseSummary result;
    forSamples(scope, [&](const Sample& sample){addSavings(result, sample, retainedFrames(sample));});
    return result;
}

template<class Scope> MinimiseSummary apply(Scope& scope) {
    const auto summary = analyze(scope);
    if (!summary.changed()) return summary;
    struct Prepared {Sample* sample;std::vector<std::int16_t> prefix;};
    std::vector<Prepared> prepared;prepared.reserve(summary.samplesChanged);
    forSamples(scope, [&](Sample& sample) {
        const auto keep = retainedFrames(sample);
        if (keep != sample.pcm.size())
            prepared.push_back({&sample, std::vector<std::int16_t>(sample.pcm.begin(), sample.pcm.begin() + keep)});
    });
    // std::vector with the default allocator has a noexcept swap. Every
    // validation and allocation has finished; no partial batch can escape.
    for (auto& change : prepared) change.sample->pcm.swap(change.prefix);
    // The old vectors (including their capacity) are destroyed with prepared.
    return summary;
}
} // namespace

MinimiseSummary analyzeMinimise(const Sample& sample) { return analyze(sample); }
MinimiseSummary analyzeMinimise(const Instrument& instrument) { return analyze(instrument); }
MinimiseSummary analyzeMinimise(const Document& document) { return analyze(document); }
MinimiseSummary minimise(Sample& sample) { return apply(sample); }
MinimiseSummary minimise(Instrument& instrument) { return apply(instrument); }
MinimiseSummary minimise(Document& document) { return apply(document); }

} // namespace brick::edit
