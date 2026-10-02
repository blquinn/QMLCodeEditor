#include "core/displaymap.h"

#include <QtCore/QPromise>
#include <QtCore/QThreadPool>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace qce {

namespace {

constexpr qsizetype kScanAhead = 64;   // rows scanned beyond what a query needs in a huge line
constexpr qsizetype kMaxCached = 4096; // lines whose break columns are kept
constexpr qsizetype kPinnedRows = 64;  // lines with more rows than this are never evicted
constexpr qsizetype kEagerUnits = 1 << 18; // text an edit wraps at once; more is left to later

} // namespace

DisplayMap::DisplayMap(const TextDocument *document, QObject *parent)
    : QObject(parent), m_document(document), m_fold(document) {
  connect(document, &TextDocument::changed, this, &DisplayMap::onChanged);
  connect(document, &TextDocument::textReset, this, [this] {
    resetWrap();
    emit reset();
  });
  connect(&m_watcher, &QFutureWatcher<ChunkResult>::finished, this, [this] {
    m_running = false;
    const ChunkResult result = m_watcher.result();
    if (result.generation == m_generation && result.version == m_document->version())
      applyChunk(result);
    pumpBackground();
  });
}

void DisplayMap::setBackgroundWrapping(bool enabled) {
  m_background = enabled;
  if (enabled)
    pumpBackground();
}

void DisplayMap::resetWrap() {
  ++m_generation;
  m_cursor = 0;
  m_breaks.clear();
  m_wrap = WrapMap();
  if (wrapEnabled())
    m_wrap.reset(m_fold.lineCount(), 1, true);
  pumpBackground();
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

  // Only the lines the edit touched are wrapped again, up to a budget of text: a huge line, or the
  // lines of a big paste or a file being loaded, are left as estimates to be wrapped when something
  // asks (or in the background).
  qsizetype budget = kEagerUnits;
  QList<WrapMap::Entry> entries;
  entries.reserve(newLines);
  const Rope &rope = m_document->rope();
  for (qsizetype i = 0; i < newLines; ++i) {
    const qsizetype line = first + i;
    if (budget <= 0) {
      entries.append({1, true});
      continue;
    }
    const qsizetype length = rope.lineLength(line);
    if (length > kHugeLine || budget < length) {
      if (length <= kHugeLine) {
        entries.append({1, true});
      } else if (kept) {
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
      budget -= length + 1;
      entries.append({quint32(lb.starts.size() + 1), false});
    }
  }
  if (oldLines == newLines)
    m_wrap.setLines(first, entries);
  else
    m_wrap.splice(first, oldLines, entries);
  const qsizetype newRows = m_wrap.firstRowOfLine(first + newLines) - firstRow;
  emit rowsChanged(firstRow, oldRows, newRows);
  pumpBackground();
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
  // A line known to be exact has lost its break columns to eviction or never had them (a worker
  // counted its rows): find them again.
  if (!m_wrap.isEstimated(line))
    extend(line, lb, -1);
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

namespace qce {

namespace {
constexpr qsizetype kChunkUnits = 1 << 20;
constexpr qsizetype kChunkLines = 20000;
constexpr qsizetype kHugeStepRows = 4000;
} // namespace

// Starts a worker on the next stretch of estimated lines unless one is running. The worker reads a
// snapshot and the config, nothing else of ours; its answer is applied on this thread, and dropped
// when the text or the config has moved on.
void DisplayMap::pumpBackground() {
  if (!m_background || m_running || !wrapEnabled())
    return;
  qsizetype line = m_wrap.nextEstimated(m_cursor);
  if (line < 0)
    line = m_wrap.nextEstimated(0);
  if (line < 0) {
    emit wrapProgress(0);
    return;
  }

  const TextSnapshot snapshot = m_document->snapshot();
  const WrapConfig config = m_config;
  const quint64 generation = m_generation;
  const qsizetype lineCount = snapshot.rope().lineCount();
  QList<qsizetype> knownStarts;
  qreal knownIndent = 0;
  bool huge = snapshot.rope().lineLength(line) > kHugeLine;
  if (huge) {
    const LineBreaks &lb = breaksFor(line);
    knownStarts = lb.starts;
    knownIndent = lb.indent;
  }

  auto promise = std::make_shared<QPromise<ChunkResult>>();
  m_watcher.setFuture(promise->future());
  promise->start();
  m_running = true;
  QThreadPool::globalInstance()->start([=] {
    const Rope &rope = snapshot.rope();
    ChunkResult result;
    result.generation = generation;
    result.version = snapshot.version();
    result.firstLine = line;
    if (huge) {
      result.huge = true;
      result.hugeStarts = knownStarts;
      result.hugeKnown = knownStarts.size();
      const qsizetype length = rope.lineLength(line);
      const qsizetype scanned = knownStarts.isEmpty() ? 0 : knownStarts.last();
      const qsizetype stopped = wrapRows(
        rope, rope.lineStart(line), length, config, knownIndent, scanned, knownStarts.isEmpty(), kHugeStepRows,
        result.hugeStarts
      );
      result.hugeComplete = stopped >= length;
    } else {
      qsizetype units = 0;
      for (qsizetype l = line; l < lineCount && result.rows.size() < kChunkLines && units < kChunkUnits; ++l) {
        const qsizetype length = rope.lineLength(l);
        if (length > kHugeLine)
          break; // the next chunk takes it on its own
        QList<qsizetype> starts;
        const qsizetype start = rope.lineStart(l);
        wrapRows(rope, start, length, config, wrapIndent(config, rope, start, length), 0, true,
                 std::numeric_limits<qsizetype>::max(), starts);
        result.rows.append(quint32(starts.size() + 1));
        units += length + 1;
      }
    }
    promise->addResult(std::move(result));
    promise->finish();
  });
}

void DisplayMap::applyChunk(const ChunkResult &r) {
  if (r.huge) {
    LineBreaks &lb = breaksFor(r.firstLine);
    if (lb.starts.size() != r.hugeKnown)
      return; // a query scanned further meanwhile; the next chunk picks up from there
    lb.starts = r.hugeStarts;
    lb.complete = r.hugeComplete;
    storeRows(r.firstLine, lb);
    m_cursor = r.hugeComplete ? r.firstLine + 1 : r.firstLine;
  } else {
    QList<WrapMap::Entry> entries;
    entries.reserve(r.rows.size());
    for (quint32 rows : r.rows)
      entries.append({rows, false});
    const qsizetype firstRow = m_wrap.firstRowOfLine(r.firstLine);
    const qsizetype oldRows = m_wrap.firstRowOfLine(r.firstLine + r.rows.size()) - firstRow;
    m_wrap.setLines(r.firstLine, entries);
    const qsizetype newRows = m_wrap.firstRowOfLine(r.firstLine + r.rows.size()) - firstRow;
    m_cursor = r.firstLine + r.rows.size();
    if (oldRows != newRows)
      emit rowsReestimated(firstRow, oldRows, newRows);
  }
  emit wrapProgress(m_wrap.estimatedLineCount());
}

} // namespace qce
