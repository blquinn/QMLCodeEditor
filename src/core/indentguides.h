#ifndef QCE_INDENTGUIDES_H
#define QCE_INDENTGUIDES_H

#include <QtCore/QList>
#include <QtCore/QStringView>

namespace qce {

class Rope;

// The width in cells of a line's leading whitespace, tabs expanded to `tabWidth` stops. -1 when the
// line is empty or only whitespace.
int indentColumns(QStringView lineText, int tabWidth);

// The indentation each line from `firstLine` to `lastLine` (inclusive) shows guides for. A blank
// line has none of its own, so it takes it from the nearest text lines above and below: their
// indent when equal, else the smaller one plus `unit` capped at the larger one. A search for
// text goes at most `maxSearch` lines out and counts as indent 0 where it finds none.
QList<int> effectiveIndents(
  const Rope &rope, qsizetype firstLine, qsizetype lastLine, int tabWidth, int unit, qsizetype maxSearch = 1000
);

} // namespace qce

#endif // QCE_INDENTGUIDES_H
