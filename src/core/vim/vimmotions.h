#ifndef QCE_VIM_VIMMOTIONS_H
#define QCE_VIM_VIMMOTIONS_H

#include "core/rope.h"

#include <QtCore/QStringView>

namespace qce::vim {

// Vim's motions as pure functions over a rope. Offsets are UTF-16 units on character boundaries;
// results are offsets (never moved by an operator's rules: the handler decides whether a motion is
// exclusive, inclusive or linewise). Counts are at least 1.

// What a position holds: blank (space, tab), keyword character, other punctuation, the line break of a
// non-empty line, or an empty line. With `big` (vim's WORD) keyword and punctuation are one class.
enum class CharClass : quint8 { Blank, Word, Punct, Newline, Empty };
CharClass classAt(const Rope &rope, qsizetype pos, bool big = false);

// Scans stop after this many units (lines for paragraphs), so a motion in a huge file stays within
// the keystroke budget: a paragraph motion in text without blank lines, or a sentence motion in text
// without sentence ends, does not move rather than scan the whole document.
constexpr qsizetype kMotionScanLimit = 20000;
constexpr qsizetype kParagraphScanLines = 10000;

// w / W: start of the next word; an empty line counts as a word. The rope's length at the end.
qsizetype wordForward(const Rope &rope, qsizetype pos, int count, bool big);
// b / B.
qsizetype wordBackward(const Rope &rope, qsizetype pos, int count, bool big);
// e / E: the last character of the word (the next one when already on a last character).
qsizetype wordEnd(const Rope &rope, qsizetype pos, int count, bool big);
// ge / gE: the last character of the previous word.
qsizetype wordEndBackward(const Rope &rope, qsizetype pos, int count, bool big);

// First character of `line` that is not a blank; the line's end when it is all blanks.
qsizetype firstNonBlank(const Rope &rope, qsizetype line);
// Last character of `line` that is not a blank (g_); the line's start when it is all blanks.
qsizetype lastNonBlank(const Rope &rope, qsizetype line);

// f/t/F/T within the line of `pos`: the `count`th occurrence of `ch` (one code point) after (or
// before) `pos`; with `till` one character short of it. `skipAdjacent` makes `;` after t/T step
// over a match right next to the cursor. -1 when there is none.
qsizetype findCharInLine(
  const Rope &rope, qsizetype pos, QStringView ch, int count, bool forward, bool till,
  bool skipAdjacent = false
);

// } and {: the next/previous empty line (the end/start of the document when there is none within
// kParagraphScanLines lines; then the position does not change).
qsizetype paragraphForward(const Rope &rope, qsizetype pos, int count);
qsizetype paragraphBackward(const Rope &rope, qsizetype pos, int count);

// ) and (: the next/previous sentence start. A sentence ends at . ! ? (then closing brackets or quotes)
// followed by a blank or the line end; empty lines are sentence boundaries too.
bool isSentenceStart(const Rope &rope, qsizetype pos);
qsizetype sentenceForward(const Rope &rope, qsizetype pos, int count);
qsizetype sentenceBackward(const Rope &rope, qsizetype pos, int count);

// Display columns (tabs advance to the next multiple of `tabWidth`, everything else is one cell).
int virtualColumn(const Rope &rope, qsizetype offset, int tabWidth);
// Width in cells of the character at `offset` when it starts at display column `column`.
int cellWidthAt(const Rope &rope, qsizetype offset, int column, int tabWidth);
// The character of `line` whose cells include display column `column`; the line's end (and
// *pastEnd = true) when the line is shorter.
qsizetype
offsetAtVirtualColumn(const Rope &rope, qsizetype line, int column, int tabWidth, bool *pastEnd = nullptr);

// Is the offset on the last character of its line, or is the line empty? (Where normal mode puts
// the cursor at most.)
bool isAtLineEnd(const Rope &rope, qsizetype offset);
// The last character position of `line` (its start for an empty line).
qsizetype lastCharOffset(const Rope &rope, qsizetype line);

} // namespace qce::vim

#endif // QCE_VIM_VIMMOTIONS_H
