#ifndef QCE_SELECTIONSET_H
#define QCE_SELECTIONSET_H

#include "core/anchorset.h"
#include "core/selection.h"
#include "core/textdocument.h"

#include <QtCore/QObject>

#include <limits>

namespace qce {

// The editor's selections (ADR 0005). It always holds at least one: a lone cursor is a set of one.
// Every selection is a pair of anchors in the document's AnchorSet, so selections follow edits made
// by anyone. Selections are kept sorted by start with overlapping or touching ones merged when the
// set is replaced; the primary selection is the one the editor scrolls to and reports.
class SelectionSet : public QObject {
  Q_OBJECT
public:
  static constexpr qreal NoGoal = std::numeric_limits<qreal>::quiet_NaN();

  explicit SelectionSet(TextDocument *document, QObject *parent = nullptr);
  ~SelectionSet() override;

  TextDocument *document() const { return m_document; }

  int count() const { return int(m_entries.size()); }
  int primaryIndex() const { return m_primary; }
  Selection at(int index) const;
  Selection primary() const { return at(m_primary); }
  SelectionList selections() const;

  // Replaces the set. Offsets are clamped and moved off code point interiors, the list is sorted
  // and overlapping or touching selections are merged (the primary follows its merge). An empty
  // list becomes a cursor at 0. Goal columns are reset.
  void set(const SelectionList &selections, int primary = 0);
  void setSingle(qsizetype anchor, qsizetype head) { set({{anchor, head}}); }
  void setSingle(qsizetype cursor) { set({{cursor, cursor}}); }

  // Makes another selection the primary one.
  void setPrimary(int index);
  // Adds one selection and makes it primary. Without an overlap this touches only its own anchors
  // (merging, if it does overlap, is the same as set()).
  void add(Selection selection);
  // Drops every selection but the primary.
  void collapseToPrimary();

  // The first selection that ends at or after `offset` (count() if none), by binary search.
  int lowerBound(qsizetype offset) const;
  // The selection holding `offset` (touching its ends counts), or -1.
  int indexAt(qsizetype offset) const;

  // Sticky x for vertical movement (INPUT-03), per selection; NoGoal when unset.
  qreal goalX(int index) const { return m_entries[size_t(index)].goalX; }
  void setGoalX(int index, qreal x) { m_entries[size_t(index)].goalX = x; }

  // Holds back changed() while the document is edited in several steps; one signal at the end if
  // anything moved.
  class Batch {
  public:
    explicit Batch(SelectionSet &set) : m_set(set) { ++m_set.m_batch; }
    ~Batch() {
      if (--m_set.m_batch == 0 && m_set.m_dirty)
        m_set.notify();
    }
    Q_DISABLE_COPY(Batch)

  private:
    SelectionSet &m_set;
  };

signals:
  // The selections were replaced or moved by an edit.
  void changed();

private:
  struct Entry {
    AnchorId anchor = InvalidAnchor;
    AnchorId head = InvalidAnchor;
    qreal goalX = NoGoal;
  };

  void removeAnchors();
  void notify();
  void assign(const SelectionList &list, int primary);
  void normalize();
  void onDocumentChanged(const TextChange &change);
  void onDocumentReset();

  TextDocument *m_document;
  std::vector<Entry> m_entries;
  int m_primary = 0;
  int m_batch = 0;
  bool m_dirty = false;
  bool m_needsNormalize = false;
};

} // namespace qce

#endif // QCE_SELECTIONSET_H
