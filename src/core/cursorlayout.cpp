#include "core/cursorlayout.h"

namespace qce {

namespace {

qsizetype advance(QChar c, qsizetype cell, int tabWidth) {
  return c == u'\t' ? cell + tabWidth - cell % tabWidth : cell + 1;
}

} // namespace

qreal GridCursorLayout::xForOffset(qsizetype offset) const {
  const Rope &rope = m_document->rope();
  const TextPosition pos = rope.positionAt(offset);
  const qsizetype start = rope.lineStart(pos.line);
  qsizetype cell = 0;
  for (qsizetype i = 0; i < pos.column; ++i)
    cell = advance(rope.at(start + i), cell, m_tabWidth);
  return cell;
}

qsizetype GridCursorLayout::offsetForX(const DisplayRow &row, qreal x) const {
  const Rope &rope = m_document->rope();
  const qsizetype start = rope.lineStart(row.line);
  qsizetype cell = 0;
  qsizetype column = row.startColumn;
  for (qsizetype i = 0; i < row.startColumn; ++i)
    cell = advance(rope.at(start + i), cell, m_tabWidth);
  while (column < row.endColumn) {
    const qsizetype next = advance(rope.at(start + column), cell, m_tabWidth);
    if (x < (cell + next) / 2.0)
      break;
    cell = next;
    ++column;
  }
  return rope.snapToCodePoint(start + column);
}

} // namespace qce
