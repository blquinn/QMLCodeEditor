#ifndef QCE_SELECTION_H
#define QCE_SELECTION_H

#include <QtCore/QList>
#include <QtCore/QtGlobal>

namespace qce {

// A selection as the two offsets that define it; `head` is the end the cursor sits at. This is the
// payload undo restores; INPUT-01 builds the full SelectionSet on top of it (ADR 0005).
struct Selection {
  qsizetype anchor = 0;
  qsizetype head = 0;

  qsizetype start() const { return qMin(anchor, head); }
  qsizetype end() const { return qMax(anchor, head); }
  bool isEmpty() const { return anchor == head; }
  friend constexpr bool operator==(const Selection &, const Selection &) = default;
};

using SelectionList = QList<Selection>;

} // namespace qce

#endif // QCE_SELECTION_H
