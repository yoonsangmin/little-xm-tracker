#include "model/Arrangement.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace brick::edit {
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void checkPatternId(int pattern) {
    require(pattern >= 0 && pattern <= 255, "Pattern number must be 00..FF");
}
void checkHeader(const Document& document) {
    require(document.channels >= 1 && document.channels <= 32, "Channel count must be 1..32");
    require(!document.orders.empty() && document.orders.size() <= 256, "A song needs 1..256 orders");
    require(document.restart < document.orders.size(), "Restart order is outside the song");
    require(document.patterns.size() <= 256, "Maximum 256 patterns");
}
void checkDocument(const Document& document) {
    checkHeader(document);
    for (const auto& pattern : document.patterns) {
        require(pattern.rows >= 1 && pattern.rows <= 256, "Pattern length must be 1..256");
        require(pattern.cells.size() == std::size_t(pattern.rows) * document.channels,
                "Pattern storage dimensions are invalid");
    }
}
Pattern blankPattern(int rows, int channels) {
    Pattern result;
    result.rows = static_cast<std::uint16_t>(rows);
    result.cells.resize(std::size_t(rows) * std::size_t(channels));
    return result;
}
int freePatternId(const Document& document, int excluded = -1) {
    std::array<bool, 256> reserved{};
    for (auto pattern : document.orders) reserved[pattern] = true;
    if (excluded >= 0) reserved[std::size_t(excluded)] = true;
    for (int id = 0; id < 256; ++id) {
        const auto index = static_cast<std::size_t>(id);
        if (reserved[index]) continue;
        if (index >= document.patterns.size()) return id;
        const auto& pattern = document.patterns[index];
        // A custom length is authored data even when the pattern is silent.
        if (pattern.rows == 64 && std::none_of(pattern.cells.begin(), pattern.cells.end(),
                [](const Cell& cell) { return cell.note || cell.instrument || cell.volume || cell.effect || cell.param; }))
            return id;
    }
    throw std::runtime_error("No unused pattern slots remain");
}
int allocatePattern(Document& document, Pattern pattern, int excluded = -1) {
    const int id = freePatternId(document, excluded);
    // Prepare the complete replacement before touching the document. This
    // protects the original even if allocation fails while filling a gap.
    auto replacement = document.patterns;
    if (std::size_t(id) < replacement.size()) {
        replacement[std::size_t(id)] = std::move(pattern);
    } else {
        replacement.reserve(std::size_t(id) + 1);
        while (replacement.size() < std::size_t(id))
            replacement.push_back(blankPattern(64, document.channels));
        replacement.push_back(std::move(pattern));
    }
    document.patterns.swap(replacement);
    return id;
}
void checkOrderIndex(const Document& document, int index) {
    require(index >= 0 && std::size_t(index) < document.orders.size(), "Order index is outside the song");
}
} // namespace

int patternUseCount(const Document& document, int pattern) {
    checkHeader(document);
    checkPatternId(pattern);
    return static_cast<int>(std::count(document.orders.begin(), document.orders.end(),
                                       static_cast<std::uint8_t>(pattern)));
}

int availablePatternSlots(const Document& document) {
    checkHeader(document);
    const int highestOrder = static_cast<int>(*std::max_element(document.orders.begin(), document.orders.end()));
    return std::max(static_cast<int>(document.patterns.size()), highestOrder + 1);
}

int createPattern(Document& document, int rows) {
    checkDocument(document);
    require(rows >= 1 && rows <= 256, "Pattern length must be 1..256");
    return allocatePattern(document, blankPattern(rows, document.channels));
}

int duplicatePattern(Document& document, int source) {
    checkDocument(document);
    checkPatternId(source);
    auto copy = std::size_t(source) < document.patterns.size() ? document.patterns[std::size_t(source)] :
        blankPattern(64, document.channels);
    return allocatePattern(document, std::move(copy), source);
}

void insertOrder(Document& document, int index, int pattern) {
    checkDocument(document);
    checkPatternId(pattern);
    require(document.orders.size() < 256, "Maximum 256 orders");
    require(index >= 0 && std::size_t(index) <= document.orders.size(), "Order insertion position is outside the song");
    auto replacement = document.orders;
    replacement.insert(replacement.begin() + index, static_cast<std::uint8_t>(pattern));
    const auto restart = static_cast<std::uint16_t>(document.restart + (index <= document.restart ? 1 : 0));
    document.orders.swap(replacement);
    document.restart = restart;
}

void removeOrder(Document& document, int index) {
    checkDocument(document);
    checkOrderIndex(document, index);
    require(document.orders.size() > 1, "A song needs at least one order");
    int restart = document.restart;
    if (index < restart) --restart;
    else if (index == restart) restart = std::min(index, static_cast<int>(document.orders.size()) - 2);
    document.orders.erase(document.orders.begin() + index);
    document.restart = static_cast<std::uint16_t>(restart);
}

void moveOrder(Document& document, int from, int to) {
    checkDocument(document);
    checkOrderIndex(document, from);
    checkOrderIndex(document, to);
    if (from == to) return;
    int restart = document.restart;
    if (restart == from) restart = to;
    else if (from < to && restart > from && restart <= to) --restart;
    else if (from > to && restart >= to && restart < from) ++restart;
    if (from < to)
        std::rotate(document.orders.begin() + from, document.orders.begin() + from + 1, document.orders.begin() + to + 1);
    else
        std::rotate(document.orders.begin() + to, document.orders.begin() + from, document.orders.begin() + from + 1);
    document.restart = static_cast<std::uint16_t>(restart);
}

} // namespace brick::edit
