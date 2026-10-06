#ifndef QCE_DECORATIONSET_H
#define QCE_DECORATIONSET_H

#include "core/anchorset.h"
#include "core/textdocument.h"

#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtGui/QColor>
#include <QtGui/QImage>

#include <optional>
#include <vector>

namespace qce {

// What a decoration looks like (ADR 0006, ADR 0016).
enum class DecorationKind : quint8 {
  Underline,     // a straight line under the text
  Squiggle,      // a wavy line under the text
  Background,    // a filled band behind the text
  GutterIcon,    // an icon in a decoration column, on the first row of the line
  EndOfLineText, // virtual text after the end of the line the range starts on
  InlineText     // virtual text inside a line, at the start of the range (inlay hints)
};
constexpr int kDecorationKindCount = 6;
constexpr quint32 decorationKindBit(DecorationKind kind) { return 1u << unsigned(kind); }
constexpr quint32 kAllDecorationKinds = (1u << kDecorationKindCount) - 1;

// Severities share LSP's numbering; 0 means none.
enum DecorationSeverity { NoSeverity = 0, ErrorSeverity = 1, WarningSeverity = 2, InfoSeverity = 3, HintSeverity = 4 };

// A decoration to add. Offsets are UTF-16 offsets into the document and are clamped to it.
struct DecorationSpec {
  qsizetype start = 0;
  qsizetype end = 0;
  DecorationKind kind = DecorationKind::Underline;
  QColor color;  // invalid: the theme's color for the severity (or for the kind)
  QString text;  // EndOfLineText and InlineText
  int severity = NoSeverity;
  int priority = 0; // the highest wins where decorations of one kind overlap
  int tag = 0;      // the host's own number, handed back with every query (the diagnostic model uses it)
  QImage icon;      // GutterIcon; null: a built-in glyph for the severity
  // Which way the ends lean when text is typed exactly at them. By default typing at either edge
  // leaves the decoration as it is.
  Gravity startGravity = Gravity::Right;
  Gravity endGravity = Gravity::Left;
};

// A decoration as it is now: offsets follow the edits made since it was added.
struct Decoration {
  int id = 0;
  int layer = 0;
  qsizetype start = 0;
  qsizetype end = 0; // never before start
  DecorationKind kind = DecorationKind::Underline;
  QColor color;
  QString text;
  int severity = NoSeverity;
  int priority = 0;
  int tag = 0;
  QImage icon;
};

// Ranges that follow edits and carry a look (ADR 0006): squiggles, underlines, backgrounds, gutter
// icons and virtual text. Every decoration is a pair of anchors in the document's AnchorSet, so edits
// move them without rewriting them. Decorations belong to a layer; a host or the diagnostic model
// replaces its own layer in one call without touching the others.
//
// Entries are kept sorted by start offset (anchors keep their order under edits), so a query is a
// binary search plus a walk over the entries that can reach back into the range: `m_maxSpan` bounds
// how far back that is. A decoration longer than kLongSpan would make that bound useless for every
// query, so those few are held apart and checked one by one.
//
// Only the calls that add or remove decorations emit changed(); edits to the text move decorations
// without any signal (the editor already repaints on every edit).
class DecorationSet : public QObject {
  Q_OBJECT
public:
  static constexpr qsizetype kLongSpan = 1 << 16;

  explicit DecorationSet(TextDocument *document, QObject *parent = nullptr);
  ~DecorationSet() override;

  qsizetype size() const { return qsizetype(m_entries.size() + m_long.size()); }
  // How many decorations of `kind` there are; lets callers skip work when there are none.
  qsizetype count(DecorationKind kind) const { return m_counts[int(kind)]; }
  qsizetype layerSize(int layer) const;

  // Adds one decoration and returns its id.
  int add(const DecorationSpec &spec, int layer = 0);
  bool remove(int id);
  // Replaces everything in `layer` with `specs` (in any order). Cheaper than add() one by one.
  void setLayer(int layer, const QList<DecorationSpec> &specs);
  void clearLayer(int layer);
  void clear();

  // Decorations of the given kinds that touch [firstOffset, lastOffset], ordered by start. Touching
  // counts, so an empty decoration at lastOffset is included.
  QList<Decoration> query(qsizetype firstOffset, qsizetype lastOffset, quint32 kinds = kAllDecorationKinds) const;
  // The same for whole lines, [start of firstLine, end of lastLine].
  QList<Decoration> queryLines(qsizetype firstLine, qsizetype lastLine, quint32 kinds = kAllDecorationKinds) const;
  Decoration decoration(int id) const; // id 0 when there is none
  bool contains(int id) const;

  // Walks decorations in start order, for "next/previous" navigation: the first one whose start is
  // after `offset` (or the last one before it) that `accept` takes. nullopt when there is none.
  template <typename Accept> std::optional<Decoration> next(qsizetype offset, Accept accept) const;
  template <typename Accept> std::optional<Decoration> previous(qsizetype offset, Accept accept) const;

  // Checks the ordering and bookkeeping; for tests.
  bool validate() const;

signals:
  // Decorations of `kinds` (a mask of decorationKindBit) were added or removed on lines
  // [firstLine, lastLine].
  void changed(qsizetype firstLine, qsizetype lastLine, quint32 kinds);

private:
  struct Entry {
    int id = 0;
    int layer = 0;
    AnchorId start = InvalidAnchor;
    AnchorId end = InvalidAnchor;
    DecorationKind kind = DecorationKind::Underline;
    int severity = NoSeverity;
    int priority = 0;
    int tag = 0;
    QColor color;
    QString text;
    QImage icon;
  };
  struct Touched {
    qsizetype first = -1, last = -1;
    quint32 kinds = 0;
    void add(qsizetype offset, DecorationKind kind) {
      first = first < 0 ? offset : qMin(first, offset);
      last = qMax(last, offset);
      kinds |= decorationKindBit(kind);
    }
  };

  Decoration toDecoration(const Entry &entry) const;
  Entry makeEntry(const DecorationSpec &spec, int layer);
  void destroy(const Entry &entry, Touched &touched);
  void insertEntry(Entry entry);
  size_t lowerBound(qsizetype offset) const;
  void onChanged(const TextChange &change);
  void emitTouched(const Touched &touched);
  qsizetype startOf(const Entry &entry) const { return m_document->anchors().offset(entry.start); }
  qsizetype endOf(const Entry &entry) const;
  void recomputeSpan() const;

  TextDocument *m_document;
  std::vector<Entry> m_entries; // sorted by start offset
  std::vector<Entry> m_long;    // unordered
  int m_nextId = 1;
  qsizetype m_counts[kDecorationKindCount] = {};
  // Entries whose start leans Left: an edit can put those out of order with their neighbours, so
  // the slice an edit touched is sorted again.
  qsizetype m_leftStarts = 0;
  // Upper bound of end - start over m_entries: an edit grows a span by at most what it inserts, and
  // the exact value is recomputed once the bound has drifted.
  mutable qsizetype m_maxSpan = 0;
  mutable qsizetype m_drift = 0;
};

template <typename Accept> std::optional<Decoration> DecorationSet::next(qsizetype offset, Accept accept) const {
  std::optional<Decoration> best;
  for (size_t i = lowerBound(offset + 1); i < m_entries.size(); ++i) {
    const Decoration d = toDecoration(m_entries[i]);
    if (accept(d)) {
      best = d;
      break;
    }
  }
  for (const Entry &entry : m_long) {
    if (startOf(entry) <= offset || (best && startOf(entry) >= best->start))
      continue;
    const Decoration d = toDecoration(entry);
    if (accept(d))
      best = d;
  }
  return best;
}

template <typename Accept> std::optional<Decoration> DecorationSet::previous(qsizetype offset, Accept accept) const {
  std::optional<Decoration> best;
  for (size_t i = lowerBound(offset); i > 0; --i) {
    const Decoration d = toDecoration(m_entries[i - 1]);
    if (accept(d)) {
      best = d;
      break;
    }
  }
  for (const Entry &entry : m_long) {
    if (startOf(entry) >= offset || (best && startOf(entry) <= best->start))
      continue;
    const Decoration d = toDecoration(entry);
    if (accept(d))
      best = d;
  }
  return best;
}

} // namespace qce

#endif // QCE_DECORATIONSET_H
