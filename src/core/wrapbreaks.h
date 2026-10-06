#ifndef QCE_WRAPBREAKS_H
#define QCE_WRAPBREAKS_H

#include "core/rope.h"
#include "core/wrapmeasure.h"

#include <QtCore/QList>

#include <memory>

namespace qce {

enum class WrapMode : quint8 {
  Off,      // one row per line
  Viewport, // rows are as wide as the viewport
  Column    // rows are `column` cells wide
};

// What soft wrap needs to know (ADR 0011). Widths are in the pixels of `measure`.
struct WrapConfig {
  WrapMode mode = WrapMode::Off;
  qreal width = 0;            // Viewport mode: the viewport's width
  int column = 80;            // Column mode
  bool wordBreak = true;      // break after whitespace when possible, otherwise between characters
  bool hangingIndent = false; // continuation rows start under the line's indentation
  int extraIndent = 0;        // more columns of continuation indent
  std::shared_ptr<const WrapMeasure> measure;

  bool enabled() const { return mode != WrapMode::Off && measure; }
  // Width available to the text of a row, never less than a few cells so progress is possible.
  qreal rowWidth() const;

  friend bool operator==(const WrapConfig &a, const WrapConfig &b) {
    return a.mode == b.mode && a.width == b.width && a.column == b.column && a.wordBreak == b.wordBreak &&
           a.hangingIndent == b.hangingIndent && a.extraIndent == b.extraIndent && a.measure == b.measure;
  }
};

// Indent in pixels of the continuation rows of a line: its leading whitespace when hanging indent
// is on, plus the configured extra, never more than half a row.
qreal wrapIndent(const WrapConfig &config, const Rope &rope, qsizetype lineStart, qsizetype lineLength);

// Width that virtual text (an inlay hint) adds to a line, attached to the character it travels with:
// a hint that leans on the text after it belongs to the character at its column, one that leans on
// the text before it to the character before (DIAG-06). Wrapping adds `width` to that character, so a
// row break never separates a hint from it. Lists are sorted by `column`.
struct InlineSpan {
  qsizetype column = 0; // the character the width belongs to
  qreal width = 0;
};

// Wraps the line [lineStart, lineStart + lineLength) beginning with the row that starts at
// `startColumn` (`firstRow`: it is the line's first row, which can be wider than the rest).
// Appends the start column of each following row to `starts` and stops after `maxRows` rows or at
// the end of the line. Returns the column where scanning stopped: the start of the next row to
// scan, or `lineLength` when the line is finished.
//
// A row always holds at least one character cluster. When breaking at words, whitespace may hang
// past the right edge. `inlays` (optional, for this line) are widths added to characters.
qsizetype wrapRows(
  const Rope &rope, qsizetype lineStart, qsizetype lineLength, const WrapConfig &config, qreal indent,
  qsizetype startColumn, bool firstRow, qsizetype maxRows, QList<qsizetype> &starts,
  const QList<InlineSpan> *inlays = nullptr
);

} // namespace qce

#endif // QCE_WRAPBREAKS_H
