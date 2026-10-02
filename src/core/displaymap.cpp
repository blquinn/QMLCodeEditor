#include "core/displaymap.h"

namespace qce {

DisplayMap::DisplayMap(const TextDocument *document, QObject *parent)
    : QObject(parent), m_document(document) {
  connect(document, &TextDocument::changed, this, [this](const TextChange &change) {
    // Rows are lines for now: the edit replaced the lines it touched with the lines it produced.
    const qsizetype first = change.startPos.line;
    emit rowsChanged(first, change.oldEndPos.line - first + 1, change.newEndPos.line - first + 1);
  });
  connect(document, &TextDocument::textReset, this, &DisplayMap::reset);
}

qsizetype DisplayMap::rowCount() const { return m_document->rope().lineCount(); }

qsizetype DisplayMap::lineForRow(qsizetype row) const { return qBound<qsizetype>(0, row, rowCount() - 1); }

DisplayRow DisplayMap::rowAt(qsizetype row) const {
  const qsizetype line = lineForRow(row);
  return {line, 0, m_document->rope().lineLength(line)};
}

qsizetype DisplayMap::firstRowOfLine(qsizetype line) const {
  return qBound<qsizetype>(0, line, rowCount() - 1);
}

qsizetype DisplayMap::rowCountOfLine(qsizetype) const { return 1; }

qsizetype DisplayMap::rowForPosition(TextPosition position) const { return firstRowOfLine(position.line); }

} // namespace qce
