#include "model/Cleanup.hpp"

#include <array>

namespace brick::edit {
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void validateStructure(const Document& document) {
    require(document.channels >= 1 && document.channels <= 32, "Channel count must be 1..32");
    require(!document.orders.empty() && document.orders.size() <= 256, "A song needs 1..256 orders");
    require(document.restart < document.orders.size(), "Restart order is outside the song");
    require(document.patterns.size() <= 256, "Maximum 256 patterns");
    require(document.instruments.size() <= 128, "Maximum 128 instruments");
    for (const auto& pattern : document.patterns) {
        require(pattern.rows >= 1 && pattern.rows <= 256, "Pattern length must be 1..256");
        require(pattern.cells.size() == std::size_t(pattern.rows) * document.channels,
                "Pattern storage dimensions are invalid");
    }
}
} // namespace

CleanupPlan analyzeCleanup(const Document& document, CleanupScope scope) {
    validateStructure(document);
    const bool cleanPatterns = scope != CleanupScope::Instruments;
    const bool cleanInstruments = scope != CleanupScope::Patterns;
    CleanupPlan plan;
    plan.patternMap.fill(-1);
    plan.instrumentMap.fill(-1);
    plan.instrumentMap[0] = 0;
    std::array<bool, 256> usedPatterns{};
    std::array<bool, 129> usedInstruments{};
    if (cleanPatterns) {
        for (const auto id : document.orders) usedPatterns[id] = true;
    } else usedPatterns.fill(true);

    int nextPattern = 0;
    for (std::size_t id = 0; id < usedPatterns.size(); ++id) {
        if (!usedPatterns[id]) {
            if (id < document.patterns.size()) ++plan.patternsRemoved;
            continue;
        }
        plan.patternMap[id] = nextPattern;
        if (int(id) != nextPattern) ++plan.patternsRenumbered;
        ++nextPattern;
        if (!cleanInstruments || id >= document.patterns.size()) continue; // Empty, omitted XM pattern.
        for (const auto& cell : document.patterns[id].cells) {
            if (!cell.instrument) continue;
            require(cell.instrument <= document.instruments.size(),
                    "Cannot clean up: a used pattern references a missing instrument");
            usedInstruments[cell.instrument] = true;
        }
    }

    if (!cleanInstruments) {
        for (std::size_t id = 1; id < plan.instrumentMap.size(); ++id) plan.instrumentMap[id] = int(id);
        return plan;
    }
    int nextInstrument = 1;
    for (std::size_t id = 1; id <= document.instruments.size(); ++id) {
        if (!usedInstruments[id]) { ++plan.instrumentsRemoved; continue; }
        plan.instrumentMap[id] = nextInstrument;
        if (int(id) != nextInstrument) ++plan.instrumentsRenumbered;
        ++nextInstrument;
    }
    return plan;
}

CleanupPlan cleanupUnused(Document& document, CleanupScope scope) {
    const auto plan = analyzeCleanup(document, scope);
    if (!plan.changed()) return plan;

    // Build only the retained containers. The old document remains valid
    // until every allocation/copy has succeeded, then noexcept swaps commit.
    auto orders = document.orders;
    for (auto& id : orders) id = static_cast<std::uint8_t>(plan.patternMap[id]);
    std::vector<Pattern> patterns;
    patterns.reserve(document.patterns.size() - std::size_t(plan.patternsRemoved));
    for (std::size_t id = 0; id < document.patterns.size(); ++id) {
        if (plan.patternMap[id] < 0) continue;
        patterns.push_back(document.patterns[id]);
        if (scope != CleanupScope::Patterns)
            for (auto& cell : patterns.back().cells)
                cell.instrument = static_cast<std::uint8_t>(plan.instrumentMap[cell.instrument]);
    }
    std::vector<Instrument> instruments;
    if (scope != CleanupScope::Patterns) {
        instruments.reserve(document.instruments.size() - std::size_t(plan.instrumentsRemoved));
        for (std::size_t id = 1; id <= document.instruments.size(); ++id)
            if (plan.instrumentMap[id] >= 0) instruments.push_back(document.instruments[id - 1]);
    }

    document.orders.swap(orders);
    document.patterns.swap(patterns);
    if (scope != CleanupScope::Patterns) document.instruments.swap(instruments);
    return plan;
}

} // namespace brick::edit
