#ifndef QCE_CURSORLAYOUT_H
#define QCE_CURSORLAYOUT_H

#include "core/displaymap.h"
#include "core/textdocument.h"

namespace qce {

// The geometry that cursor movement needs and `core` cannot have: where an offset sits horizontally
// and which offset a horizontal position hits. The editor implements it from its text metrics and
// shaped layouts; GridCursorLayout is the headless version used by tests. Vertical structure comes
// from the DisplayMap, never from here (ADR 0004).
class CursorLayout {
public:
  virtual ~CursorLayout() = default;
  // x of `offset` within its row, in content coordinates.
  virtual qreal xForOffset(qsizetype offset) const = 0;
  // The offset on `row` nearest to `x`; never outside the row's column range.
  virtual qsizetype offsetForX(const DisplayRow &row, qreal x) const = 0;
  // How many rows a page up/down moves.
  virtual qsizetype pageRows() const = 0;
};

// One cell per UTF-16 unit and tab stops every `tabWidth` cells: exact for a monospace font and
// text without wide or combining characters. Rows of a wrapped line count cells from their own
// start, after their indent, as the editor draws them; give it the DisplayMap to know the rows.
class GridCursorLayout : public CursorLayout {
public:
  explicit GridCursorLayout(
    const TextDocument *document, int tabWidth = 4, qsizetype pageRows = 20, const DisplayMap *map = nullptr
  )
      : m_document(document), m_map(map), m_tabWidth(tabWidth), m_pageRows(pageRows) {}

  qreal xForOffset(qsizetype offset) const override;
  qsizetype offsetForX(const DisplayRow &row, qreal x) const override;
  qsizetype pageRows() const override { return m_pageRows; }

private:
  const TextDocument *m_document;
  const DisplayMap *m_map;
  int m_tabWidth;
  qsizetype m_pageRows;
};

} // namespace qce

#endif // QCE_CURSORLAYOUT_H
