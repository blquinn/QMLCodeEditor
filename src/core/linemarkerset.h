#ifndef QCE_LINEMARKERSET_H
#define QCE_LINEMARKERSET_H

#include "core/anchorset.h"
#include "core/textdocument.h"

#include <QtCore/QList>
#include <QtCore/QObject>

#include <vector>

namespace qce {

// Ranges of whole lines that follow edits (ADR 0006): gutter marks, change bars, breakpoints. A marker
// is a pair of anchors, one at the start of its first line (right gravity) and one at the end of its
// last line (left gravity). Typing inside it grows it; a line break inserted at the start of its first
// line pushes it down with the text instead of growing it upwards; a line break at the end of its last
// line does not extend it; deleting all of its text leaves it on the line the deletion joined.
//
// Anchors keep their order under edits, so markers stay sorted by start offset and a query is a
// binary search plus a walk over the markers that can reach back into the range.
class LineMarkerSet : public QObject {
  Q_OBJECT
public:
  struct Marker {
    int id = 0;
    qsizetype firstLine = 0;
    qsizetype lastLine = 0;
    int kind = 0;
    int priority = 0;
  };

  explicit LineMarkerSet(TextDocument *document, QObject *parent = nullptr);
  ~LineMarkerSet() override;

  qsizetype size() const { return qsizetype(m_entries.size()); }

  // Adds a marker over [firstLine, lastLine] (clamped to the text) and returns its id.
  int add(qsizetype firstLine, qsizetype lastLine, int kind = 0, int priority = 0);
  bool remove(int id);
  void clear();
  // Marks [firstLine, lastLine] as `kind`, merging with markers of that kind that overlap it or touch
  // it, so repeated edits to neighbouring lines keep a handful of markers, not one per keystroke.
  void markRange(qsizetype firstLine, qsizetype lastLine, int kind, int priority = 0);

  // Markers touching [firstLine, lastLine], ordered by start.
  QList<Marker> query(qsizetype firstLine, qsizetype lastLine) const;
  Marker marker(int id) const;
  bool contains(int id) const;

signals:
  // The lines of markers that were added, removed or changed; the whole range for clear().
  void changed();

private:
  struct Entry {
    int id;
    AnchorId start;
    AnchorId end;
    int kind;
    int priority;
  };

  Marker toMarker(const Entry &entry) const;
  void eraseAt(size_t index);
  // First entry whose start offset is >= offset.
  size_t lowerBound(qsizetype offset) const;
  void onChanged(const TextChange &change);
  void recomputeSpan() const;

  TextDocument *m_document;
  std::vector<Entry> m_entries;
  int m_nextId = 1;
  // Upper bound of end - start over all entries: an edit grows a span by at most what it inserts, and
  // the exact value is recomputed once the bound has drifted.
  mutable qsizetype m_maxSpan = 0;
  mutable qsizetype m_drift = 0;
};

} // namespace qce

#endif // QCE_LINEMARKERSET_H
