#include "model/Arrangement.hpp"
#include "model/EditOperations.hpp"
#include "io/Codec.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
int checks = 0;
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
bool same(const brick::Cell& a, const brick::Cell& b) {
    return a.note == b.note && a.instrument == b.instrument && a.volume == b.volume &&
        a.effect == b.effect && a.param == b.param;
}
bool same(const brick::Pattern& a, const brick::Pattern& b) {
    return a.rows == b.rows && a.cells.size() == b.cells.size() &&
        std::equal(a.cells.begin(), a.cells.end(), b.cells.begin(),
                   [](const brick::Cell& first, const brick::Cell& second) { return same(first, second); });
}
brick::Pattern emptyPattern(int rows, int channels) {
    brick::Pattern result;
    result.rows = static_cast<std::uint16_t>(rows);
    result.cells.resize(std::size_t(rows) * std::size_t(channels));
    return result;
}
brick::Document fixture() {
    auto document = brick::makeDemoDocument();
    document.patterns[0].at(0, 0, document.channels) = {49, 1, 0xB7, 4, 0x48};
    document.patterns[0].at(1, 1, document.channels) = {0, 0, 0xF3, 0x0B, 2};
    auto second = document.patterns[0];
    second.rows = 17;
    second.cells.resize(std::size_t(second.rows) * document.channels);
    second.at(0, 0, document.channels).note = 60;
    document.patterns.push_back(second);
    document.orders = {0, 1, 0};
    document.restart = 2;
    return document;
}
template<class Fn> void rejectsUnchanged(brick::Document& document, Fn fn, const char* message) {
    const auto before = brick::io::encodeXm(document);
    bool rejected = false;
    try { fn(); } catch (const std::runtime_error&) { rejected = true; }
    check(rejected, message);
    check(brick::io::encodeXm(document) == before, "Rejected arrangement edit changed the document");
}

void sharedPatternsAndClone() {
    auto document = fixture();
    check(brick::edit::patternUseCount(document, 0) == 2, "Repeated orders must share one pattern");
    check(brick::edit::patternUseCount(document, 1) == 1, "Single pattern reference count");
    const auto before = document;
    const auto originalOrders = document.orders;
    const int cloned = brick::edit::duplicatePattern(document, 0);
    check(cloned == 2 && document.orders == originalOrders, "Duplicate creates an unarranged pattern without rewiring orders");
    check(brick::edit::patternUseCount(document, cloned) == 0, "New duplicate is unused until arranged");
    check(document.patterns[cloned].rows == before.patterns[0].rows, "Duplicate preserves pattern length");
    for (std::size_t n = 0; n < before.patterns[0].cells.size(); ++n)
        check(same(document.patterns[cloned].cells[n], before.patterns[0].cells[n]), "Duplicate preserves both effect columns and every raw field");
    document.patterns[cloned].at(0, 0, document.channels).note = 72;
    check(document.patterns[0].at(0, 0, document.channels).note == 49, "Duplicate PCM-independent cells must not alias source pattern");
    check(document.instruments[0].samples[0].pcm == before.instruments[0].samples[0].pcm, "Arrangement edits preserve sample PCM");
    const auto changed = brick::io::encodeXm(document);
    const auto delta = brick::edit::DocumentDelta::between(before, document);
    delta.undo(document);
    check(brick::io::encodeXm(document) == brick::io::encodeXm(before), "History restores the pre-clone document including pattern count");
    delta.redo(document);
    check(brick::io::encodeXm(document) == changed, "History reapplies independent pattern creation and content");
}

void omittedAndUnusedPatterns() {
    auto document = fixture();
    document.patterns.clear();
    document.orders = {0, 7, 0};
    check(brick::edit::availablePatternSlots(document) == 8, "Logical pattern range includes omitted referenced patterns");
    const int created = brick::edit::createPattern(document, 23);
    check(created == 1, "Allocation skips an omitted pattern already used by Song");
    check(document.patterns[0].rows == 64 && document.patterns[0].cells.size() == std::size_t(64) * document.channels,
          "Filling an allocation gap retains the omitted pattern's 64-row meaning");
    check(document.patterns[1].rows == 23 && document.orders == std::vector<std::uint8_t>({0, 7, 0}),
          "Fresh pattern length does not change Song arrangement");
    const int clone = brick::edit::duplicatePattern(document, 7);
    check(clone == 2 && document.patterns[clone].rows == 64, "Missing source duplicates as an empty 64-row pattern");
    check(std::all_of(document.patterns[clone].cells.begin(), document.patterns[clone].cells.end(),
                     [](const brick::Cell& c) {return same(c, {});}), "Omitted duplicate remains silent");
    const auto roundtrip = brick::io::decodeXm(brick::io::encodeXm(document));
    check(roundtrip.orders == document.orders && roundtrip.patterns.size() == 3 && roundtrip.patterns[1].rows == 23,
          "XM roundtrip preserves unarranged patterns and omitted order references");
    const auto patternCount = document.patterns.size();
    brick::edit::removeOrder(document, 1);
    check(document.patterns.size() == patternCount, "Removing an order never deletes a pattern");
    brick::edit::insertOrder(document, 1, 255);
    check(document.patterns.size() == patternCount && brick::edit::availablePatternSlots(document) == 256,
          "Assigning an omitted high pattern does not materialize it");
}

void reusablePatternSlots() {
    auto document = fixture();
    document.patterns.resize(256, document.patterns[0]);
    document.patterns[73] = emptyPattern(64, document.channels);
    const auto before = document;
    const auto beforeBytes = brick::io::encodeXm(document);
    const int duplicate = brick::edit::duplicatePattern(document, 0);
    check(duplicate == 73 && document.patterns.size() == 256,
          "A full pattern table reuses an empty unused middle slot without shifting numbers");
    check(same(document.patterns[73], before.patterns[0]) && document.orders == before.orders &&
          document.restart == before.restart, "Reused duplicate preserves source contents and all Order references");
    for (std::size_t id = 0; id < document.patterns.size(); ++id)
        if (id != 73) check(same(document.patterns[id], before.patterns[id]), "Reusing a hole preserves every other pattern");
    const auto afterBytes = brick::io::encodeXm(document);
    check(brick::io::encodeXm(brick::io::decodeXm(afterBytes)) == afterBytes,
          "A reused numbered pattern survives XM roundtrip");
    const auto delta = brick::edit::DocumentDelta::between(before, document);
    delta.undo(document);
    check(brick::io::encodeXm(document) == beforeBytes && document.patterns.size() == 256,
          "Undo restores the empty middle slot while retaining all pattern numbers");
    delta.redo(document);
    check(brick::io::encodeXm(document) == afterBytes, "Redo restores a duplicate in the reused slot");

    document = before;
    check(brick::edit::createPattern(document, 19) == 73 && document.patterns.size() == 256 &&
          same(document.patterns[73], emptyPattern(19, document.channels)) && document.orders == before.orders,
          "New Empty Pattern also reuses a middle slot and applies the requested row length");
    document = before;
    rejectsUnchanged(document, [&]{brick::edit::duplicatePattern(document, 73);},
                     "An unreferenced empty source cannot be reused as its own duplicate when no other slot is free");
    document.patterns[73] = emptyPattern(23, document.channels);
    rejectsUnchanged(document, [&]{brick::edit::createPattern(document);},
                     "No free slot remains when the only silent slot has an intentional custom length");
}

void protectedPatternSlots() {
    auto document = fixture();
    const auto source = document.patterns[0];
    document.patterns.assign(12, emptyPattern(64, document.channels));
    document.patterns[0] = source;
    document.orders = {0, 1, 1, 7};
    document.patterns[2].cells.back().instrument = 1;
    document.patterns[3].cells.back().volume = 0x10;
    document.patterns[4].cells.back().effect = 4;
    document.patterns[5].cells.back().param = 0x37;
    document.patterns[6].cells.back().note = 49;
    document.patterns[8] = emptyPattern(23, document.channels);
    const auto before = document;
    const int duplicate = brick::edit::duplicatePattern(document, 0);
    check(duplicate == 9 && document.patterns.size() == 12 && same(document.patterns[9], source),
          "Allocation picks the lowest safe hole before appending, preserving single-field content and custom lengths");
    for (std::size_t id = 0; id < 9; ++id)
        check(same(document.patterns[id], before.patterns[id]),
              "Referenced silent patterns, instrument/volume/FX/parameter-only cells and nonstandard rows are protected");
    check(document.orders == before.orders && brick::edit::patternUseCount(document, 1) == 2,
          "Allocation leaves shared empty Order references unchanged");
    check(brick::edit::createPattern(document, 17) == 10 && document.patterns.size() == 12 &&
          same(document.patterns[10], emptyPattern(17, document.channels)),
          "New pattern creation uses the next lowest safe slot without trimming the pattern table");

    document.patterns.assign(2, emptyPattern(64, document.channels));
    document.orders = {1};document.restart = 0;
    check(brick::edit::duplicatePattern(document, 0) == 2 && document.patterns.size() == 3 &&
          same(document.patterns[0], emptyPattern(64, document.channels)),
          "An empty unarranged source stays reserved while its duplicate is allocated elsewhere");
}

void orderIdentityAndRestart() {
    auto document = fixture();
    document.orders = {10, 20, 30, 40};
    document.restart = 2;
    const auto originalPatterns = document.patterns;
    brick::edit::insertOrder(document, 1, 99);
    check(document.orders == std::vector<std::uint8_t>({10, 99, 20, 30, 40}) && document.restart == 3,
          "Insertion preserves the restart order's identity");
    brick::edit::moveOrder(document, 0, 4);
    check(document.orders == std::vector<std::uint8_t>({99, 20, 30, 40, 10}) && document.restart == 2,
          "Forward stable move shifts intervening orders and restart");
    brick::edit::moveOrder(document, 4, 1);
    check(document.orders == std::vector<std::uint8_t>({99, 10, 20, 30, 40}) && document.restart == 3,
          "Backward stable move shifts intervening orders and restart");
    brick::edit::moveOrder(document, 3, 0);
    check(document.orders == std::vector<std::uint8_t>({30, 99, 10, 20, 40}) && document.restart == 0,
          "Moving the restart order directly retains its identity");
    brick::edit::removeOrder(document, 0);
    check(document.restart == 0 && document.orders[0] == 99, "Deleting restart chooses its surviving successor");
    document.restart = 3;
    brick::edit::removeOrder(document, 1);
    check(document.restart == 2 && document.orders[2] == 40, "Deleting before restart adjusts its position");
    brick::edit::removeOrder(document, 2);
    check(document.restart == 1 && document.orders[1] == 20, "Deleting final restart chooses the last surviving order");
    check(same(document.patterns[0].at(1, 1, document.channels), originalPatterns[0].at(1, 1, document.channels)),
          "Rearranging orders preserves literal Bxx jumps and volume effects");
    const auto roundtrip = brick::io::decodeXm(brick::io::encodeXm(document));
    check(roundtrip.orders == document.orders && roundtrip.restart == document.restart,
          "XM roundtrip preserves order arrangement and restart");
}

void limitsAndRejections() {
    auto document = fixture();
    rejectsUnchanged(document, [&]{brick::edit::createPattern(document, 0);}, "Zero-row new pattern must be rejected");
    rejectsUnchanged(document, [&]{brick::edit::createPattern(document, 257);}, "Oversized new pattern must be rejected");
    rejectsUnchanged(document, [&]{brick::edit::duplicatePattern(document, -1);}, "Negative duplicate source must be rejected");
    rejectsUnchanged(document, [&]{brick::edit::duplicatePattern(document, 256);}, "Out-of-range duplicate source must be rejected");
    rejectsUnchanged(document, [&]{brick::edit::insertOrder(document, -1, 0);}, "Negative insertion position must be rejected");
    rejectsUnchanged(document, [&]{brick::edit::insertOrder(document, 4, 0);}, "Insertion past the end must be rejected");
    rejectsUnchanged(document, [&]{brick::edit::insertOrder(document, 0, 256);}, "Invalid pattern reference must be rejected");
    rejectsUnchanged(document, [&]{brick::edit::removeOrder(document, 3);}, "Removing missing order must be rejected");
    rejectsUnchanged(document, [&]{brick::edit::moveOrder(document, 0, 3);}, "Moving to a missing order must be rejected");
    rejectsUnchanged(document, [&]{brick::edit::moveOrder(document, -1, 0);}, "Negative move source must be rejected");
    document.orders = {0};document.restart = 0;
    rejectsUnchanged(document, [&]{brick::edit::removeOrder(document, 0);}, "Last order must be retained");
    document.orders.resize(256, 0);document.restart = 255;
    rejectsUnchanged(document, [&]{brick::edit::insertOrder(document, 256, 0);}, "Order capacity must not silently overflow");
    document.patterns.clear();
    for (int i = 0; i < 256; ++i) document.orders[std::size_t(i)] = static_cast<std::uint8_t>(i);
    rejectsUnchanged(document, [&]{brick::edit::createPattern(document);}, "All 256 omitted referenced patterns are reserved");
    document = fixture();
    const auto occupied = document.patterns[1];
    document.patterns.resize(256, occupied);
    rejectsUnchanged(document, [&]{brick::edit::duplicatePattern(document, 0);}, "Stored nonempty unused patterns still occupy their IDs");
    const auto previous = document.channels;
    document.channels = 0;
    bool rejected = false;
    try { brick::edit::createPattern(document); } catch (const std::runtime_error&) {rejected = true;}
    check(rejected && document.channels == 0 && document.patterns.size() == 256, "Malformed dimensions are rejected without repairs or mutation");
    document.channels = previous;
}
} // namespace

int main() {
    try {
        sharedPatternsAndClone();
        omittedAndUnusedPatterns();
        reusablePatternSlots();
        protectedPatternSlots();
        orderIdentityAndRestart();
        limitsAndRejections();
        std::cout << "Arrangement tests passed: " << checks << " checks; shared/unused/omitted patterns, stable order moves, restart, limits and XM roundtrip.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Arrangement test failure: " << e.what() << '\n';
        return 1;
    }
}
