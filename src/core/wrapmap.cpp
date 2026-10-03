#include "core/wrapmap.h"

#include <algorithm>

namespace qce {

namespace {
constexpr qsizetype kBlockLines = 1024;
}

void WrapMap::summarize(Block &block) {
  block.rows = 0;
  block.estimated = 0;
  for (quint32 v : std::as_const(block.lines)) {
    block.rows += visibleRows(v);
    block.estimated += (v & kEstimated) ? 1 : 0;
  }
}

void WrapMap::fenwickAdd(QList<qsizetype> &tree, qsizetype block, qsizetype delta) {
  for (qsizetype i = block + 1; i < tree.size(); i += i & -i)
    tree[i] += delta;
}

// Sum of the first `blocks` blocks.
qsizetype WrapMap::fenwickPrefix(const QList<qsizetype> &tree, qsizetype blocks) {
  qsizetype sum = 0;
  for (qsizetype i = blocks; i > 0; i -= i & -i)
    sum += tree[i];
  return sum;
}

// Index of the block that contains the `target`-th unit (0-based) and what remains inside it. A
// target at or past the total returns the block count.
qsizetype WrapMap::fenwickFind(const QList<qsizetype> &tree, qsizetype target, qsizetype *remainder) {
  const qsizetype n = tree.size() - 1;
  qsizetype pos = 0;
  qsizetype step = 1;
  while (step * 2 <= n)
    step *= 2;
  for (; step > 0; step /= 2) {
    if (pos + step <= n && tree[pos + step] <= target) {
      pos += step;
      target -= tree[pos];
    }
  }
  *remainder = target;
  return pos;
}

void WrapMap::rebuildIndex() {
  const qsizetype n = qsizetype(m_blocks.size());
  m_fenLines.assign(n + 1, 0);
  m_fenRows.assign(n + 1, 0);
  m_fenEstimated.assign(n + 1, 0);
  m_lines = m_rows = m_estimated = 0;
  for (qsizetype b = 0; b < n; ++b) {
    m_fenLines[b + 1] = m_blocks[b].lines.size();
    m_fenRows[b + 1] = m_blocks[b].rows;
    m_fenEstimated[b + 1] = m_blocks[b].estimated;
    m_lines += m_blocks[b].lines.size();
    m_rows += m_blocks[b].rows;
    m_estimated += m_blocks[b].estimated;
  }
  // Linear-time Fenwick construction.
  for (qsizetype i = 1; i <= n; ++i) {
    const qsizetype parent = i + (i & -i);
    if (parent <= n) {
      m_fenLines[parent] += m_fenLines[i];
      m_fenRows[parent] += m_fenRows[i];
      m_fenEstimated[parent] += m_fenEstimated[i];
    }
  }
}

void WrapMap::reset(qsizetype lines, quint32 rows, bool estimated) {
  m_blocks.clear();
  const quint32 packed = pack({rows, estimated});
  for (qsizetype done = 0; done < lines; done += kBlockLines) {
    Block block;
    block.lines.assign(qMin(kBlockLines, lines - done), packed);
    summarize(block);
    m_blocks.push_back(std::move(block));
  }
  rebuildIndex();
}

qsizetype WrapMap::blockOfLine(qsizetype line, qsizetype *offset) const {
  return fenwickFind(m_fenLines, line, offset);
}

qsizetype WrapMap::blockOfRow(qsizetype row, qsizetype *rowOffset) const {
  return fenwickFind(m_fenRows, row, rowOffset);
}

WrapMap::Entry WrapMap::entry(qsizetype line) const {
  if (m_lines == 0)
    return {};
  qsizetype offset;
  qsizetype b = blockOfLine(qBound<qsizetype>(0, line, m_lines - 1), &offset);
  return unpack(m_blocks[b].lines[offset]);
}

qsizetype WrapMap::firstRowOfLine(qsizetype line) const {
  if (line <= 0)
    return 0;
  if (line >= m_lines)
    return m_rows;
  qsizetype offset;
  const qsizetype b = blockOfLine(line, &offset);
  qsizetype rows = fenwickPrefix(m_fenRows, b);
  const QList<quint32> &lines = m_blocks[b].lines;
  for (qsizetype i = 0; i < offset; ++i)
    rows += visibleRows(lines[i]);
  return rows;
}

qsizetype WrapMap::lineAtRow(qsizetype row, qsizetype *rowInLine) const {
  if (m_lines == 0) {
    if (rowInLine)
      *rowInLine = 0;
    return 0;
  }
  row = qBound<qsizetype>(0, row, m_rows - 1);
  qsizetype rowOffset;
  const qsizetype b = blockOfRow(row, &rowOffset);
  qsizetype line = fenwickPrefix(m_fenLines, b);
  const QList<quint32> &lines = m_blocks[b].lines;
  for (qsizetype i = 0; i < lines.size(); ++i) {
    const qsizetype rows = visibleRows(lines[i]);
    if (rowOffset < rows) {
      if (rowInLine)
        *rowInLine = rowOffset;
      return line + i;
    }
    rowOffset -= rows;
  }
  Q_UNREACHABLE_RETURN(m_lines - 1);
}

qsizetype WrapMap::nextEstimated(qsizetype fromLine) const {
  if (m_estimated == 0 || fromLine >= m_lines)
    return -1;
  fromLine = qMax<qsizetype>(0, fromLine);
  qsizetype offset;
  qsizetype b = blockOfLine(fromLine, &offset);
  qsizetype blockStart = fromLine - offset;
  for (; b < qsizetype(m_blocks.size()); ++b, offset = 0) {
    const Block &block = m_blocks[b];
    if (block.estimated > 0)
      for (qsizetype i = offset; i < block.lines.size(); ++i)
        if (block.lines[i] & kEstimated)
          return blockStart + i;
    blockStart += block.lines.size();
  }
  return -1;
}

void WrapMap::setLines(qsizetype first, const QList<Entry> &entries) {
  qsizetype i = 0;
  while (i < entries.size() && first + i < m_lines) {
    qsizetype offset;
    const qsizetype b = blockOfLine(first + i, &offset);
    Block &block = m_blocks[b];
    qsizetype deltaRows = 0, deltaEstimated = 0;
    for (; offset < block.lines.size() && i < entries.size(); ++offset, ++i) {
      const quint32 old = block.lines[offset];
      const quint32 now = pack(entries[i]) | (old & kHidden);
      deltaRows += visibleRows(now) - visibleRows(old);
      deltaEstimated += ((now & kEstimated) ? 1 : 0) - ((old & kEstimated) ? 1 : 0);
      block.lines[offset] = now;
    }
    block.rows += deltaRows;
    block.estimated += deltaEstimated;
    m_rows += deltaRows;
    m_estimated += deltaEstimated;
    fenwickAdd(m_fenRows, b, deltaRows);
    fenwickAdd(m_fenEstimated, b, deltaEstimated);
  }
}

void WrapMap::setHidden(qsizetype first, qsizetype count, bool hidden) {
  first = qMax<qsizetype>(0, first);
  const qsizetype end = qMin(first + count, m_lines);
  while (first < end) {
    qsizetype offset;
    const qsizetype b = blockOfLine(first, &offset);
    Block &block = m_blocks[b];
    qsizetype deltaRows = 0;
    for (; offset < block.lines.size() && first < end; ++offset, ++first) {
      const quint32 old = block.lines[offset];
      const quint32 now = hidden ? (old | kHidden) : (old & ~kHidden);
      deltaRows += visibleRows(now) - visibleRows(old);
      block.lines[offset] = now;
    }
    block.rows += deltaRows;
    m_rows += deltaRows;
    fenwickAdd(m_fenRows, b, deltaRows);
  }
}

void WrapMap::splice(qsizetype first, qsizetype oldCount, const QList<Entry> &entries) {
  first = qBound<qsizetype>(0, first, m_lines);
  oldCount = qBound<qsizetype>(0, oldCount, m_lines - first);

  // The blocks touched by the edit: from the one holding `first` to the one holding the last
  // removed line (or `first` itself for a pure insertion).
  qsizetype firstOffset = 0, lastOffset = 0;
  qsizetype b0, b1;
  if (m_blocks.empty()) {
    m_blocks.emplace_back();
    b0 = b1 = 0;
  } else if (first >= m_lines) {
    b0 = b1 = qsizetype(m_blocks.size()) - 1;
    firstOffset = lastOffset = m_blocks[b0].lines.size();
  } else {
    b0 = blockOfLine(first, &firstOffset);
    if (oldCount == 0) {
      b1 = b0;
      lastOffset = firstOffset;
    } else {
      b1 = blockOfLine(first + oldCount - 1, &lastOffset);
      ++lastOffset; // one past the last removed line
    }
  }

  QList<quint32> combined = m_blocks[b0].lines.first(firstOffset);
  combined.reserve(combined.size() + entries.size() + 1);
  for (const Entry &e : entries)
    combined.append(pack(e));
  combined += m_blocks[b1].lines.sliced(lastOffset);

  std::vector<Block> replacement;
  if (!combined.isEmpty()) {
    const qsizetype pieces = (combined.size() + kBlockLines - 1) / kBlockLines;
    const qsizetype each = (combined.size() + pieces - 1) / pieces;
    for (qsizetype at = 0; at < combined.size(); at += each) {
      Block block;
      block.lines = combined.sliced(at, qMin(each, combined.size() - at));
      summarize(block);
      replacement.push_back(std::move(block));
    }
  }
  m_blocks.erase(m_blocks.begin() + b0, m_blocks.begin() + b1 + 1);
  m_blocks.insert(
    m_blocks.begin() + b0, std::make_move_iterator(replacement.begin()),
    std::make_move_iterator(replacement.end())
  );
  compactIfFragmented();
  rebuildIndex();
}

// Edits that keep splitting small blocks leave many tiny ones; re-chunk when they pile up.
void WrapMap::compactIfFragmented() {
  qsizetype lines = 0;
  for (const Block &b : m_blocks)
    lines += b.lines.size();
  if (qsizetype(m_blocks.size()) <= lines / (kBlockLines / 4) + 16)
    return;
  QList<quint32> all;
  all.reserve(lines);
  for (const Block &b : m_blocks)
    all += b.lines;
  m_blocks.clear();
  for (qsizetype at = 0; at < all.size(); at += kBlockLines) {
    Block block;
    block.lines = all.sliced(at, qMin(kBlockLines, all.size() - at));
    summarize(block);
    m_blocks.push_back(std::move(block));
  }
}

} // namespace qce
