#ifndef QCE_DISPLAYMAP_H
#define QCE_DISPLAYMAP_H

#include "core/foldmap.h"
#include "core/textchange.h"
#include "core/textdocument.h"

#include <QtCore/QObject>

namespace qce {

// What one display row shows: a column range of one buffer line.
struct DisplayRow {
  qsizetype line = 0;
  qsizetype startColumn = 0;
  qsizetype endColumn = 0; // exclusive; line content only, never the line break
  qsizetype rowInLine = 0;
  qsizetype rowsInLine = 1;
  qreal indent = 0; // hanging indent of a continuation row, in pixels

  bool isFirst() const { return rowInLine == 0; }
  bool isLast() const { return rowInLine + 1 >= rowsInLine; }
  // The last column a cursor can rest on here. A cursor at the end of a soft-wrapped row would be
  // the start of the next one, so it stops one unit short.
  qsizetype lastCursorColumn() const { return isLast() ? endColumn : endColumn - 1; }
};

// The only authority on how buffer text maps to on-screen rows (ADR 0004). Rendering, scrolling,
// hit-testing and cursor movement ask it and never assume one buffer line is one row.
//
// Layers: buffer lines -> FoldMap (identity until FOLD-01) -> wrap -> display rows. At the moment
// the wrap layer is the identity too (row == line).
class DisplayMap : public QObject {
  Q_OBJECT
public:
  explicit DisplayMap(const TextDocument *document, QObject *parent = nullptr);

  const TextDocument *document() const { return m_document; }

  qsizetype rowCount() const;
  // Buffer line shown on `row` (clamped to the valid rows).
  qsizetype lineForRow(qsizetype row) const;
  DisplayRow rowAt(qsizetype row) const;
  // First row of `line` and how many rows it occupies (a folded-away line has none).
  qsizetype firstRowOfLine(qsizetype line) const;
  qsizetype rowCountOfLine(qsizetype line) const;
  // The row showing buffer `position`.
  qsizetype rowForPosition(TextPosition position) const;

signals:
  // Rows [firstRow, firstRow + oldCount) were replaced by newCount rows; rows after shift.
  void rowsChanged(qsizetype firstRow, qsizetype oldCount, qsizetype newCount);
  // Everything is new (text reset); rebuild whatever you derived from row numbers.
  void reset();

private:
  const TextDocument *m_document;
  FoldMap m_fold;
};

} // namespace qce

#endif // QCE_DISPLAYMAP_H
