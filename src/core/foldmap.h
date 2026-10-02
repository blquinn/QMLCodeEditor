#ifndef QCE_FOLDMAP_H
#define QCE_FOLDMAP_H

#include "core/textdocument.h"

#include <QtCore/QtGlobal>

namespace qce {

// First layer of the display map (ADR 0004): buffer lines to the lines that stay visible after
// folding. It is the identity until FOLD-01 hides ranges; the wrap layer above it already talks in
// fold lines, so folding changes this class and not its consumers.
class FoldMap {
public:
  explicit FoldMap(const TextDocument *document) : m_document(document) {}

  qsizetype lineCount() const { return m_document->rope().lineCount(); }
  qsizetype foldLineForBufferLine(qsizetype bufferLine) const {
    return qBound<qsizetype>(0, bufferLine, lineCount() - 1);
  }
  qsizetype bufferLineForFoldLine(qsizetype foldLine) const {
    return qBound<qsizetype>(0, foldLine, lineCount() - 1);
  }
  bool isVisible(qsizetype) const { return true; }

private:
  const TextDocument *m_document;
};

} // namespace qce

#endif // QCE_FOLDMAP_H
