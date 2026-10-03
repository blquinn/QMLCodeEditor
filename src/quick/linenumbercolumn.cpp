#include "quick/linenumbercolumn.h"

#include <cmath>

namespace qce {

namespace {
constexpr qsizetype kMaxCachedLabels = 1024;

int digitsOf(qsizetype n) {
  int digits = 1;
  while (n >= 10) {
    n /= 10;
    ++digits;
  }
  return digits;
}
} // namespace

LineNumberColumn::LineNumberColumn(QObject *parent) : GutterColumn(parent) {
  setSelectsLines(true);
}

void LineNumberColumn::setMode(NumberMode mode) {
  if (mode == m_mode)
    return;
  m_mode = mode;
  emit modeChanged();
  emit contentChanged();
}

void LineNumberColumn::setMinimumDigits(int digits) {
  digits = qBound(1, digits, 12);
  if (digits == m_minimumDigits)
    return;
  m_minimumDigits = digits;
  emit minimumDigitsChanged();
  emit contentChanged();
}

qreal LineNumberColumn::autoWidth(const GutterContext &context) const {
  const int digits = qMax(m_minimumDigits, digitsOf(context.lineCount));
  const qreal cell = context.metrics->cellAdvance();
  // A cell of padding either side.
  return std::ceil(qreal(digits + 2) * cell);
}

qsizetype LineNumberColumn::numberFor(qsizetype line, qsizetype cursorLine, const FoldMap *folds) const {
  const qsizetype distance = folds && folds->hasFolds()
                               ? qAbs(folds->foldLineForBufferLine(line) - folds->foldLineForBufferLine(cursorLine))
                               : qAbs(line - cursorLine);
  switch (m_mode) {
  case Absolute:
    return line + 1;
  case Relative:
    return distance;
  case Hybrid:
    return line == cursorLine ? line + 1 : distance;
  }
  return line + 1;
}

std::shared_ptr<LineLayout> LineNumberColumn::labelFor(qsizetype number, const QFont &font) {
  if (font != m_labelFont || qsizetype(m_labels.size()) > kMaxCachedLabels) {
    m_labels.clear();
    m_labelFont = font;
  }
  auto &slot = m_labels[number];
  if (!slot)
    slot = makeLabelLayout(QString::number(number), font);
  return slot;
}

void LineNumberColumn::paintRows(const GutterContext &context, const QList<FramePlanRow> &rows, GutterPainter &painter) {
  const QFont &font = context.metrics->layoutFont();
  const qreal cell = context.metrics->cellAdvance();
  for (const FramePlanRow &row : rows) {
    if (!row.display.isFirst())
      continue;
    const bool current = row.display.line == context.cursorLine;
    const std::shared_ptr<LineLayout> label = labelFor(numberFor(row.display.line, context.cursorLine, &context.map->folds()), font);
    // Right-aligned, one cell from the edge. Hybrid mode puts the absolute number on the left
    // instead, as vim does.
    const bool leftAligned = m_mode == Hybrid && current;
    const qreal x = leftAligned ? cell : painter.width() - cell - label->width;
    painter.label(
      row.row, x, label, current ? context.theme->currentLineNumber() : context.theme->lineNumber()
    );
  }
}

} // namespace qce
