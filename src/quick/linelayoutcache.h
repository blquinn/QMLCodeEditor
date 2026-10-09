#ifndef QCE_LINELAYOUTCACHE_H
#define QCE_LINELAYOUTCACHE_H

#include "core/longlineindex.h"
#include "core/rope.h"

#include <QtCore/QtGlobal>
#include <QtGui/QColor>
#include <QtGui/QTextLayout>

#include <functional>
#include <list>
#include <memory>
#include <unordered_map>

namespace qce {

// Text that is laid out in a row but is not part of the document: an input-method composition
// (INPUT-05) or inline virtual text such as an inlay hint (DIAG-06). It sits before the unit at
// `column` of the row's own text, and `length` units are added to the laid-out text there.
struct Injection {
  enum Placement : quint8 {
    BeforeCursor, // a cursor at `column` is drawn after it (a hint leaning on the text after it)
    Preedit,      // the composition; the cursor is inside it
    AfterCursor   // a cursor at `column` is drawn before it (a hint leaning on the text before it)
  };
  int column = 0;
  int length = 0;
  Placement placement = BeforeCursor;
  int start = 0;     // where the injected text begins in the laid-out text
  bool pill = false; // drawn on a background pill
};

// One laid-out display row: a buffer line, or with soft wrap one column range of it. `id` is unique per layout ever created by the cache, so a node that
// remembers the id it was filled from knows whether its glyphs are still current.
// A stretch of the row (columns relative to its start) whose token style has a background (API-13).
struct StyleBackground {
  int start = 0;
  int end = 0;
  QColor color;
};

struct LineLayout {
  quint64 id = 0;
  std::unique_ptr<QTextLayout> layout;
  QString text;    // the row as stored; the layout may draw stand-in glyphs (visible whitespace)
  qreal width = 0; // natural width of the row's text, not counting the indent or virtual text after it
  qreal fullWidth = 0; // width including end-of-line virtual text (DIAG-01); equals `width` without any
  qsizetype startColumn = 0; // column of the row's first unit in its buffer line
  qreal indentX = 0;         // hanging indent: where the text starts
  bool endsLine = true;      // the last (or only) row of its buffer line
  // Text injected into the row, ordered by column and, at one column, by placement. The laid-out
  // text is `text` with these put in, so it is longer than `text`; the two index spaces are
  // converted with layoutIndex() and columnForLayoutIndex().
  QList<Injection> injections;
  QList<StyleBackground> styleBackgrounds; // drawn behind the text; empty unless the theme has any

  // A window of a very long line (PERF-01, ADR 0020): only the stretch [startColumn, startColumn +
  // text.size()) is laid out. `indentX` then includes `windowX`, the x of the window's first unit, and
  // `width`/`fullWidth` are measured to the end of the whole line, so `indentX + width` is where the line
  // ends. Columns outside the window are answered from `longIndex`.
  std::shared_ptr<const LongLineIndex> longIndex;
  Rope rope;               // the document `longIndex` describes
  qsizetype lineStart = 0; // offset of the buffer line in `rope`
  qreal windowX = 0;
  qreal windowWidth = 0; // natural width of the laid-out window itself
  bool isWindow() const { return bool(longIndex); }

  // Index in the laid-out text of the cursor at `column` (relative to the row): injected text at
  // earlier columns goes before it, and at this column so far as it leans on the text before.
  int layoutIndex(int column) const {
    int index = column;
    for (const Injection &injection : injections) {
      if (injection.column > column)
        break;
      if (injection.column < column || injection.placement != Injection::AfterCursor)
        index += injection.length;
    }
    return index;
  }
  // The column (relative to the row) a laid-out index stands for. Indexes inside injected text
  // stand for the column the text is injected at, so a click on a hint lands next to it.
  int columnForLayoutIndex(int index) const {
    int shift = 0;
    for (const Injection &injection : injections) {
      const int start = injection.column + shift;
      if (index <= start)
        break;
      if (index < start + injection.length)
        return injection.column;
      shift += injection.length;
    }
    return index - shift;
  }
  const Injection *preedit() const {
    for (const Injection &injection : injections)
      if (injection.placement == Injection::Preedit)
        return &injection;
    return nullptr;
  }
};

// LRU cache of row layouts keyed by (buffer line, row within the line) (RENDER-03). Only lines near the viewport are ever
// laid out; the cache keeps a few screens of them so scrolling back and forth costs nothing.
// Entries are shared_ptrs so a frame plan can keep using a layout the cache has since evicted.
class LineLayoutCache {
public:
  struct Stats {
    quint64 created = 0;
    quint64 hits = 0;
    quint64 evicted = 0;
  };

  explicit LineLayoutCache(qsizetype capacity = 256) : m_capacity(capacity) {}

  qsizetype capacity() const { return m_capacity; }
  // Evicts least recently used entries down to the new capacity.
  void setCapacity(qsizetype capacity);
  qsizetype size() const { return qsizetype(m_entries.size()); }
  const Stats &stats() const { return m_stats; }

  // Returns the cached layout and marks it most recently used, or null.
  std::shared_ptr<LineLayout> find(qsizetype line, qsizetype rowInLine = 0);
  // Stores a layout (assigning its id), evicting the oldest entry when full.
  std::shared_ptr<LineLayout>
  insert(qsizetype line, std::unique_ptr<QTextLayout> layout, qreal width, QString text = {}, qsizetype rowInLine = 0);

  // Lines [firstLine, firstLine + oldCount) were replaced by newCount lines: drop the replaced
  // ones and renumber everything after.
  void invalidate(qsizetype firstLine, qsizetype oldCount, qsizetype newCount);
  // Drops every line >= firstLine, or all lines when called without an argument.
  void clear(qsizetype firstLine = 0);

private:
  using Key = std::pair<qsizetype, qsizetype>; // line, row within the line
  struct KeyHash {
    size_t operator()(const Key &k) const noexcept {
      return std::hash<qsizetype>()(k.first) * 1000003u ^ std::hash<qsizetype>()(k.second);
    }
  };
  struct Entry {
    Key key;
    std::shared_ptr<LineLayout> value;
  };
  using List = std::list<Entry>; // front = most recently used

  void evictToCapacity();

  qsizetype m_capacity;
  List m_entries;
  std::unordered_map<Key, List::iterator, KeyHash> m_index;
  quint64 m_nextId = 1;
  Stats m_stats;
};

} // namespace qce

#endif // QCE_LINELAYOUTCACHE_H
