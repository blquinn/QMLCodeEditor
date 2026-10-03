#ifndef QCE_WRAPMAP_H
#define QCE_WRAPMAP_H

#include <QtCore/QList>
#include <QtCore/QtGlobal>

#include <vector>

namespace qce {

// How many display rows each line occupies, with O(log n) conversions between lines and rows
// (ADR 0004, ADR 0011). A line's count is either exact or an estimate still to be refined.
//
// Lines live in blocks of up to ~1000. Fenwick trees over the blocks hold the block sums of lines,
// rows and estimated lines, so a lookup is one descent plus a scan inside a block; an edit rewrites
// one or two blocks and rebuilds the Fenwick trees, which cost O(blocks).
class WrapMap {
public:
  struct Entry {
    quint32 rows = 1; // at least 1
    bool estimated = false;
    // Folded away: the line keeps its row count (for when it is shown again) but occupies no rows.
    bool hidden = false;
  };

  qsizetype lineCount() const { return m_lines; }
  qsizetype rowCount() const { return m_rows; }
  qsizetype estimatedLineCount() const { return m_estimated; }

  // Replaces everything with `lines` lines of `rows` rows each.
  void reset(qsizetype lines, quint32 rows, bool estimated);

  Entry entry(qsizetype line) const;
  // Rows the line occupies now: none while hidden.
  quint32 rowsOfLine(qsizetype line) const {
    const Entry e = entry(line);
    return e.hidden ? 0 : e.rows;
  }
  bool isEstimated(qsizetype line) const { return entry(line).estimated; }
  // Row index of the first row of `line`; lineCount() gives rowCount().
  qsizetype firstRowOfLine(qsizetype line) const;
  // The line holding `row` (clamped), and which of its rows that is.
  qsizetype lineAtRow(qsizetype row, qsizetype *rowInLine = nullptr) const;
  // First estimated line at or after `fromLine`, or -1.
  qsizetype nextEstimated(qsizetype fromLine) const;

  // Hides or shows lines [first, first + count); their row counts are kept.
  void setHidden(qsizetype first, qsizetype count, bool hidden);
  // Overwrites the entries of lines [first, first + entries.size()), except that each line keeps its
  // hidden flag.
  void setLines(qsizetype first, const QList<Entry> &entries);
  void setLine(qsizetype line, Entry entry) { setLines(line, {entry}); }
  // Lines [first, first + oldCount) are replaced by `entries` (any number, hidden flags included);
  // later lines shift.
  void splice(qsizetype first, qsizetype oldCount, const QList<Entry> &entries);

private:
  struct Block {
    QList<quint32> lines; // rows, with kEstimated / kHidden set on estimates / folded-away lines
    qsizetype rows = 0;
    qsizetype estimated = 0;
  };
  static constexpr quint32 kEstimated = 0x80000000u;
  static constexpr quint32 kHidden = 0x40000000u;
  static constexpr quint32 kRowsMask = 0x3fffffffu;
  static quint32 pack(Entry e) {
    return qMin(qMax<quint32>(1, e.rows), kRowsMask) | (e.estimated ? kEstimated : 0) | (e.hidden ? kHidden : 0);
  }
  static Entry unpack(quint32 v) { return {v & kRowsMask, (v & kEstimated) != 0, (v & kHidden) != 0}; }
  // Rows a packed value adds to the sums.
  static qsizetype visibleRows(quint32 v) { return (v & kHidden) ? 0 : qsizetype(v & kRowsMask); }
  static void summarize(Block &block);

  void rebuildIndex();
  void compactIfFragmented();
  // Block holding `line` and the line's index inside it.
  qsizetype blockOfLine(qsizetype line, qsizetype *offset) const;
  qsizetype blockOfRow(qsizetype row, qsizetype *rowOffset) const;
  static void fenwickAdd(QList<qsizetype> &tree, qsizetype block, qsizetype delta);
  static qsizetype fenwickPrefix(const QList<qsizetype> &tree, qsizetype blocks);
  static qsizetype fenwickFind(const QList<qsizetype> &tree, qsizetype target, qsizetype *remainder);

  std::vector<Block> m_blocks;
  QList<qsizetype> m_fenLines, m_fenRows, m_fenEstimated; // 1-based
  qsizetype m_lines = 0, m_rows = 0, m_estimated = 0;
};

} // namespace qce

#endif // QCE_WRAPMAP_H
