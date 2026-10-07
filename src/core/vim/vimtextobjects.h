#ifndef QCE_VIM_VIMTEXTOBJECTS_H
#define QCE_VIM_VIMTEXTOBJECTS_H

#include "core/rope.h"

namespace qce::vim {

// The range a text object selects: [start, end). Linewise objects (paragraphs, and the inside of a
// multi-line bracket pair) cover whole lines, with `end` at the start of the line after.
struct ObjectRange {
  bool ok = false;
  qsizetype start = 0;
  qsizetype end = 0;
  bool linewise = false;
};

// Text objects at `pos`. `kind` is the character after i/a: w W s p " ' ` ( ) b [ ] { } B < > t.
// `around` is `a` (true) or `i` (false). With a non-empty visual selection [selStart, selEnd) an
// object that was already selected grows to the next one out (words and paragraphs extend, brackets
// widen); pass selStart == selEnd for no selection.
ObjectRange textObject(
  const Rope &rope, qsizetype pos, QChar kind, bool around, int count, qsizetype selStart = 0,
  qsizetype selEnd = 0
);

} // namespace qce::vim

#endif // QCE_VIM_VIMTEXTOBJECTS_H
