#ifndef QCE_LINELAYOUTCACHE_H
#define QCE_LINELAYOUTCACHE_H

#include <QtCore/QtGlobal>
#include <QtGui/QTextLayout>

#include <functional>
#include <list>
#include <memory>
#include <unordered_map>

namespace qce {

// One laid-out display row: a buffer line, or with soft wrap one column range of it. `id` is unique per layout ever created by the cache, so a node that
// remembers the id it was filled from knows whether its glyphs are still current.
struct LineLayout {
  quint64 id = 0;
  std::unique_ptr<QTextLayout> layout;
  QString text;    // the row as stored; the layout may draw stand-in glyphs (visible whitespace)
  qreal width = 0; // natural width of the row's text, not counting the indent
  qsizetype startColumn = 0; // column of the row's first unit in its buffer line
  qreal indentX = 0;         // hanging indent: where the text starts
  bool endsLine = true;      // the last (or only) row of its buffer line
  // Input-method composition shown in this row (INPUT-05): `preeditLength` units sit at
  // `preeditColumn` (relative to the row) in the laid-out text, which is then longer than `text`.
  int preeditColumn = 0;
  int preeditLength = 0;
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
