#ifndef QCE_DISPLAYMAP_H
#define QCE_DISPLAYMAP_H

#include "core/textchange.h"
#include "core/textdocument.h"

#include <QtCore/QObject>

namespace qce {

// What one display row shows: a column range of one buffer line.
struct DisplayRow {
  qsizetype line = 0;
  qsizetype startColumn = 0;
  qsizetype endColumn = 0; // exclusive; line content only, never the line break
};

// The only authority on how buffer text maps to on-screen rows (ADR 0004). Rendering, scrolling,
// hit-testing and cursor movement ask it and never assume one buffer line is one row.
//
// Today it is the identity transform (row == line). WRAP-01 slots the fold and wrap layers in
// behind this same interface, so consumers don't change.
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
};

} // namespace qce

#endif // QCE_DISPLAYMAP_H
