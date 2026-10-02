#ifndef QCE_TEXTBOUNDARIES_H
#define QCE_TEXTBOUNDARIES_H

#include "core/rope.h"

#include <QtCore/QPair>

namespace qce {

// Code point, grapheme and word navigation over a rope. Anything that moves a cursor by "a
// character" or "a word" goes through here instead of doing offset arithmetic (ADR 0007). All
// offsets are UTF-16 units and are clamped to the rope; arguments that point into the middle of a
// grapheme or surrogate pair are tolerated but results are only meaningful from boundaries.
//
// Word classes: whitespace, word (letters, digits, marks, '_') and punctuation. With `bigWord`
// (vim's WORD) only whitespace separates words. Line breaks count as whitespace.
class TextBoundaries {
public:
  explicit TextBoundaries(const Rope &rope) : m_rope(rope) {}

  // Next/previous code point boundary; a surrogate pair moves as one.
  qsizetype nextCodePoint(qsizetype offset) const;
  qsizetype previousCodePoint(qsizetype offset) const;

  // Next/previous extended grapheme cluster boundary. A fast path covers ASCII; everything else
  // is decided by QTextBoundaryFinder on a window around the offset.
  qsizetype nextGrapheme(qsizetype offset) const;
  qsizetype previousGrapheme(qsizetype offset) const;

  // Start of the next word after `offset` (vim w/W); the rope's end if there is none.
  qsizetype nextWordStart(qsizetype offset, bool bigWord = false) const;
  // Start of the word before `offset` (vim b/B); 0 if there is none.
  qsizetype previousWordStart(qsizetype offset, bool bigWord = false) const;
  // Offset just past the last unit of the first word that ends after `offset` (vim e/E, exclusive).
  qsizetype nextWordEnd(qsizetype offset, bool bigWord = false) const;
  // Offset just past the last unit of the last word that ends before `offset` (vim ge/gE, exclusive).
  qsizetype previousWordEnd(qsizetype offset, bool bigWord = false) const;
  // The run of same-class text containing `offset` (the unit at it, or before it at the end).
  QPair<qsizetype, qsizetype> wordRangeAt(qsizetype offset, bool bigWord = false) const;

private:
  Rope m_rope;
};

} // namespace qce

#endif // QCE_TEXTBOUNDARIES_H
