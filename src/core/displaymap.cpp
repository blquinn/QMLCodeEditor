#include "core/displaymap.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace qce {

namespace {

constexpr qsizetype kScanAhead = 64;   // rows scanned beyond what a query needs in a huge line
constexpr qsizetype kMaxCached = 4096; // lines whose break columns are kept
constexpr qsizetype kPinnedRows = 64;  // lines with more rows than this are never evicted

} // namespace

DisplayMap::DisplayMap(const TextDocument *document, QObject *parent)
    : QObject(parent), m_document(document), m_fold(document) {
  connect(document, &TextDocument::changed, this, &DisplayMap::onChanged);
  connect(document, &TextDocument::textReset, this, [this] {
    resetWrap();
    emit reset();
  });
}

void DisplayMap::resetWrap() {
  m_breaks.clear();
  m_wrap = WrapMap();
  if (wrapEnabled())
    m_wrap.reset(m_fold.lineCount(), 1, true);
}

void DisplayMap::setWrapConfig(const WrapConfig &config) {
  if (config == m_config)
    return;
  m_config = config;
  resetWrap();
  emit reset();
}

qsizetype DisplayMap::lineLength(qsizetype line) const { return m_document->rope().lineLength(line); }

qsizetype DisplayMap::estimateRows(qsizetype units, qreal indent) const {
  const qreal avail = qMax(m_config.rowWidth() - indent, m_config.measure->cellAdvance());
  return qMax<qsizetype>(1, qsizetype(std::ceil(qreal(units) * m_config.measure->cellAdvance() / avail)));
}

void DisplayMap::onChanged(const TextChange &change) {
  const qsizetype first = change.startPos.line;
  const qsizetype oldLines = change.oldEndPos.line - first + 1;
  const qsizetype newLines = change.newEndPos.line - first + 1;
  if (!wrapEnabled()) {
    // Rows are lines: the edit replaced the lines it touched with the lines it produced.
    emit rowsChanged(first, oldLines, newLines);
    return;
  }

  const qsizetype firstRow = m_wrap.firstRowOfLine(first);
  const qsizetype oldRows = m_wrap.firstRowOfLine(first + oldLines) - firstRow;

  // An edit inside one huge line leaves the rows that start well before it as they were (wrapping
  // looks only a little beyond the end of a row). The indent can't have changed if the edit is
  // past the part of the line the indent is read from.
  constexpr qsizetype kSlack = 1024, kIndentScan = 4096;
  std::optional<LineBreaks> kept;
  if (oldLines == 1 && newLines == 1 && change.startPos.column > kIndentScan && m_document->rope().lineLength(first) > kHugeLine) {
    if (const auto it = m_breaks.find(first); it != m_breaks.end()) {
      kept = it->second;
      while (!kept->starts.isEmpty() && kept->starts.last() + kSlack > change.startPos.column)
        kept->starts.removeLast();
      kept->complete = false;
    }
  }
  shiftBreaks(first, oldLines, newLines);

  // Only the lines the edit touched are wrapped again. A huge one is left as an estimate and wrapped
  // when something asks (or in the background).
  QList<WrapMap::Entry> entries;
  entries.reserve(newLines);
  const Rope &rope = m_document->rope();
  for (qsizetype i = 0; i < newLines; ++i) {
    const qsizetype line = first + i;
    const qsizetype length = rope.lineLength(line);
    if (length > kHugeLine) {
      if (kept) {
        m_breaks[line] = *kept;
        const qsizetype scanned = kept->starts.isEmpty() ? 0 : kept->starts.last();
        entries.append({quint32(kept->starts.size() + estimateRows(length - scanned, kept->indent)), true});
      } else {
        entries.append({quint32(estimateRows(length, 0)), true});
      }
    } else {
      LineBreaks &lb = m_breaks[line];
      lb = {};
      lb.indent = wrapIndent(m_config, rope, rope.lineStart(line), length);
      wrapRows(rope, rope.lineStart(line), length, m_config, lb.indent, 0, true,
               std::numeric_limits<qsizetype>::max(), lb.starts);
      lb.complete = true;
      entries.append({quint32(lb.starts.size() + 1), false});
    }
  }
  if (oldLines == newLines)
    m_wrap.setLines(first, entries);
  else
    m_wrap.splice(first, oldLines, entries);
  const qsizetype newRows = m_wrap.firstRowOfLine(first + newLines) - firstRow;
  emit rowsChanged(firstRow, oldRows, newRows);
}

// Drops what was known about the replaced lines and renumbers the lines after them. Edits inside
// one huge line keep the rows that start well before the edit.
void DisplayMap::shiftBreaks(qsizetype first, qsizetype oldCount, qsizetype newCount) {
  std::unordered_map<qsizetype, LineBreaks> moved;
  moved.reserve(m_breaks.size());
  for (auto &[line, lb] : m_breaks) {
    if (line < first)
      moved.emplace(line, std::move(lb));
    else if (line >= first + oldCount)
      moved.emplace(line + newCount - oldCount, std::move(lb));
  }
  m_breaks = std::move(moved);
}

qsizetype DisplayMap::rowCount() const { return wrapEnabled() ? m_wrap.rowCount() : m_fold.lineCount(); }

DisplayMap::LineBreaks &DisplayMap::breaksFor(qsizetype line) const {
  if (const auto it = m_breaks.find(line); it != m_breaks.end())
    return it->second;
  if (m_breaks.size() >= kMaxCached) {
    for (auto it = m_breaks.begin(); it != m_breaks.end();) {
      if (it->second.complete && it->second.starts.size() <= kPinnedRows)
        it = m_breaks.erase(it);
      else
        ++it;
    }
  }
  const Rope &rope = m_document->rope();
  LineBreaks &lb = m_breaks[line];
  lb.indent = wrapIndent(m_config, rope, rope.lineStart(line), rope.lineLength(line));
  return lb;
}

void DisplayMap::extend(qsizetype line, LineBreaks &lb, qsizetype rows) const {
  if (lb.complete)
    return;
  const Rope &rope = m_document->rope();
  const qsizetype length = rope.lineLength(line);
  const qsizetype scanned = lb.starts.isEmpty() ? 0 : lb.starts.last();
  const qsizetype want = rows < 0 ? std::numeric_limits<qsizetype>::max() : qMax<qsizetype>(1, rows - lb.starts.size() + kScanAhead);
  const qsizetype stopped =
    wrapRows(rope, rope.lineStart(line), length, m_config, lb.indent, scanned, lb.starts.isEmpty(), want, lb.starts);
  lb.complete = stopped >= length;
  storeRows(line, lb);
}

// Writes what is known about a line's rows to the WrapMap: the exact count, or for a line scanned
// only partway an estimate that counts the rows found so far.
void DisplayMap::storeRows(qsizetype line, const LineBreaks &lb) const {
  qsizetype rows = lb.starts.size() + 1;
  if (!lb.complete) {
    const qsizetype scanned = lb.starts.isEmpty() ? 0 : lb.starts.last();
    rows = lb.starts.size() + estimateRows(lineLength(line) - scanned, lb.indent);
  }
  const WrapMap::Entry old = m_wrap.entry(line);
  if (old.rows == rows && old.estimated == !lb.complete)
    return;
  const qsizetype firstRow = m_wrap.firstRowOfLine(line);
  m_wrap.setLine(line, {quint32(rows), !lb.complete});
  if (old.rows != quint32(rows))
    emit const_cast<DisplayMap *>(this)->rowsReestimated(firstRow, old.rows, rows);
}

void DisplayMap::resolve(qsizetype line, qsizetype column) const {
  if (!m_wrap.isEstimated(line))
    return;
  LineBreaks &lb = breaksFor(line);
  if (lineLength(line) <= kHugeLine) {
    extend(line, lb, -1);
    return;
  }
  // A huge line is wrapped only until the row holding `column` is known.
  while (!lb.complete && (lb.starts.isEmpty() || lb.starts.last() <= column))
    extend(line, lb, lb.starts.size() + kScanAhead);
}

// The line and row-in-line shown on `row`, wrapping lines on the way until the answer is exact.
qsizetype DisplayMap::locate(qsizetype row, qsizetype *rowInLine) const {
  for (;;) {
    const qsizetype line = m_wrap.lineAtRow(row, rowInLine);
    if (!m_wrap.isEstimated(line))
      return line;
    LineBreaks &lb = breaksFor(line);
    if (lineLength(line) <= kHugeLine) {
      extend(line, lb, -1);
      continue;
    }
    if (*rowInLine < lb.starts.size() || lb.complete)
      return line;
    extend(line, lb, *rowInLine + 1);
  }
}

DisplayRow DisplayMap::makeRow(qsizetype line, qsizetype rowInLine) const {
  const qsizetype length = lineLength(line);
  const WrapMap::Entry e = m_wrap.entry(line);
  if (!e.estimated && e.rows == 1)
    return {line, 0, length, 0, 1, 0};
  const LineBreaks &lb = breaksFor(line);
  const qsizetype start = rowInLine == 0 ? 0 : lb.starts[rowInLine - 1];
  const qsizetype end = rowInLine < lb.starts.size() ? lb.starts[rowInLine] : length;
  return {line, start, end, rowInLine, qsizetype(e.rows), rowInLine == 0 ? 0 : lb.indent};
}

qsizetype DisplayMap::lineForRow(qsizetype row) const {
  if (!wrapEnabled())
    return m_fold.bufferLineForFoldLine(row);
  qsizetype rowInLine;
  return locate(row, &rowInLine);
}

DisplayRow DisplayMap::rowAt(qsizetype row) const {
  if (!wrapEnabled()) {
    const qsizetype line = m_fold.bufferLineForFoldLine(row);
    return {line, 0, lineLength(line)};
  }
  qsizetype rowInLine;
  const qsizetype line = locate(row, &rowInLine);
  return makeRow(line, rowInLine);
}

qsizetype DisplayMap::firstRowOfLine(qsizetype line) const {
  line = m_fold.foldLineForBufferLine(line);
  return wrapEnabled() ? m_wrap.firstRowOfLine(line) : line;
}

qsizetype DisplayMap::rowCountOfLine(qsizetype line) const {
  if (!wrapEnabled())
    return 1;
  line = m_fold.foldLineForBufferLine(line);
  resolve(line);
  return m_wrap.rowsOfLine(line);
}

qsizetype DisplayMap::rowForPosition(TextPosition position) const {
  const qsizetype line = m_fold.foldLineForBufferLine(position.line);
  if (!wrapEnabled())
    return line;
  resolve(line, position.column);
  const WrapMap::Entry e = m_wrap.entry(line);
  qsizetype rowInLine = 0;
  if (e.rows > 1) {
    const QList<qsizetype> &starts = breaksFor(line).starts;
    rowInLine = std::upper_bound(starts.begin(), starts.end(), position.column) - starts.begin();
  }
  return m_wrap.firstRowOfLine(line) + rowInLine;
}

} // namespace qce
