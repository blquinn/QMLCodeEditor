#include "core/wrapbreaks.h"

#include <QtCore/QChar>

#include <cmath>
#include <limits>

namespace qce {

qreal WrapConfig::rowWidth() const {
  const qreal cell = measure ? measure->cellAdvance() : 1;
  const qreal w = mode == WrapMode::Column ? column * cell : width - cell; // a cell for the cursor
  return qMax(w, 4 * cell);
}

qreal wrapIndent(const WrapConfig &config, const Rope &rope, qsizetype lineStart, qsizetype lineLength) {
  const qreal cell = config.measure->cellAdvance();
  qreal indent = config.extraIndent * cell;
  if (config.hangingIndent) {
    constexpr qsizetype kMaxScan = 4096;
    const QString lead = rope.toString(lineStart, lineStart + qMin(lineLength, kMaxScan));
    const qreal tabStop = config.measure->tabWidth() * cell;
    qreal x = 0;
    for (QChar c : lead) {
      if (c == u' ')
        x += cell;
      else if (c == u'\t')
        x += tabStop - std::fmod(x, tabStop);
      else
        break;
    }
    indent += x;
  }
  return qMin(indent, config.rowWidth() / 2);
}

namespace {

// Length in code units of the row at the start of `t`, or -1 when the window ends before the row
// does (`atLineEnd` is false). The row is the whole text when it fits and the window reaches the
// line's end.
qsizetype scanWindow(const QString &t, bool atLineEnd, qreal avail, const WrapConfig &cfg) {
  const WrapMeasure &measure = *cfg.measure;
  const qreal tabStop = measure.tabWidth() * measure.cellAdvance();
  const qsizetype n = t.size();
  qreal x = 0;
  qsizetype i = 0;
  qsizetype lastBreak = 0; // just after the last whitespace that follows text; 0 when there is none
  bool sawText = false;    // indentation is not a place to break
  bool afterJoiner = false;
  while (i < n) {
    const QChar hi = t[i];
    char32_t cp = hi.unicode();
    qsizetype len = 1;
    if (hi.isHighSurrogate()) {
      if (i + 1 >= n) {
        if (!atLineEnd)
          return -1; // the pair is cut by the window
      } else if (t[i + 1].isLowSurrogate()) {
        cp = QChar::surrogateToUcs4(hi, t[i + 1]);
        len = 2;
      }
    }
    const bool extender = i > 0 && (afterJoiner || isClusterExtender(cp));
    const bool space = cp == u' ' || cp == u'\t';
    const qreal advance = cp == u'\t' ? tabStop - std::fmod(x, tabStop) : measure.advance(cp);
    // Whitespace may hang past the edge when breaking at words; between characters it is a character.
    if (!extender && !(space && cfg.wordBreak) && i > 0 && x + advance > avail + 1e-6)
      return cfg.wordBreak && lastBreak > 0 ? lastBreak : i;
    x += advance;
    i += len;
    afterJoiner = cp == 0x200D;
    if (!space)
      sawText = true;
    else if (sawText)
      lastBreak = i;
  }
  return atLineEnd ? n : -1;
}

// The row starting at `rowStart` of a line with `remaining` units left, as a length.
qsizetype rowLength(const Rope &rope, qsizetype rowStart, qsizetype remaining, qreal avail, const WrapConfig &cfg) {
  qsizetype window = 2048;
  for (;;) {
    const qsizetype n = qMin(remaining, window);
    const qsizetype length = scanWindow(rope.toString(rowStart, rowStart + n), n == remaining, avail, cfg);
    if (length > 0)
      return length;
    window *= 4;
  }
}

} // namespace

qsizetype wrapRows(
  const Rope &rope, qsizetype lineStart, qsizetype lineLength, const WrapConfig &config, qreal indent,
  qsizetype startColumn, bool firstRow, qsizetype maxRows, QList<qsizetype> &starts
) {
  const qreal full = config.rowWidth();
  qsizetype column = startColumn;
  for (qsizetype rows = 0; rows < maxRows; ++rows) {
    if (column >= lineLength)
      return lineLength;
    const qreal avail = firstRow ? full : full - indent;
    firstRow = false;
    const qsizetype length = rowLength(rope, lineStart + column, lineLength - column, avail, config);
    if (column + length >= lineLength)
      return lineLength;
    column += length;
    starts.append(column);
  }
  return column;
}

} // namespace qce
