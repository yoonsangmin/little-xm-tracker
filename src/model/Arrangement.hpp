#pragma once

#include "model/Document.hpp"

namespace brick::edit {

int patternUseCount(const Document& document, int pattern);
// Number of selectable pattern IDs, including omitted patterns referenced by
// orders. Omitted XM patterns behave as blank 64-row patterns.
int availablePatternSlots(const Document& document);

// Allocate the first unmaterialized ID not referenced by any order. Existing
// patterns, including unused/blank ones, are never overwritten or renumbered.
// Neither operation inserts an order; callers arrange the result explicitly.
int createPattern(Document& document, int rows = 64);
int duplicatePattern(Document& document, int source);

// index is the insertion position, 0..orders.size(). A pattern reference may
// point to an omitted pattern; inserting an order does not materialize it.
void insertOrder(Document& document, int index, int pattern);
void removeOrder(Document& document, int index);
// Stable move: from/to are existing order indices. Restart follows its order;
// deleting that order chooses its successor, or the final remaining order.
// Raw pattern effects (including Bxx order jumps) are never rewritten.
void moveOrder(Document& document, int from, int to);

} // namespace brick::edit
