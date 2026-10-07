#ifndef QCE_BRACKETMATCH_H
#define QCE_BRACKETMATCH_H

#include <QtCore/QList>
#include <QtCore/QtGlobal>

#include <utility>

namespace qce {

class Rope;

// How far (UTF-16 units) a search for a partner bracket goes before giving up. A stray bracket in a
// huge file must not cost a scan of the whole document on every cursor move.
constexpr qsizetype kBracketScanLimit = 100000;

using BracketPairs = QList<std::pair<char16_t, char16_t>>;

// An opener and its closer, as offsets of the two characters.
struct BracketPair {
  qsizetype open = -1;
  qsizetype close = -1;
  bool valid() const { return open >= 0 && close >= 0; }
  friend bool operator==(const BracketPair &, const BracketPair &) = default;
};

// The partner of the bracket at `offset` (an opener or a closer of one of `pairs`), found by counting
// nesting in the raw text, so brackets in strings and comments count like any other. Pairs whose two
// characters are equal (quotes) are ignored. Invalid when `offset` is not a bracket, nothing
// matches, or the match is further than `maxScan` units away.
BracketPair findMatchingBracket(
  const Rope &rope, qsizetype offset, const BracketPairs &pairs, qsizetype maxScan = kBracketScanLimit
);

// The innermost pair that encloses `offset` (a cursor position): the nearest opener before it that
// no closer between it and `offset` pairs with, and that opener's partner. Same raw-text counting
// and `maxScan` as above (the opener must be within it, and so must the closer, from the opener).
// Invalid at the top level or when the enclosing opener is never closed.
BracketPair findEnclosingBrackets(
  const Rope &rope, qsizetype offset, const BracketPairs &pairs, qsizetype maxScan = kBracketScanLimit
);

// The offset of the bracket a cursor at `head` is next to: the character after it, else the one
// before it. -1 when neither is a bracket.
qsizetype bracketNearCursor(const Rope &rope, qsizetype head, const BracketPairs &pairs);

} // namespace qce

#endif // QCE_BRACKETMATCH_H
