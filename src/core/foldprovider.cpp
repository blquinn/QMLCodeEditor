#include "core/foldprovider.h"

#include <QtCore/QRegularExpression>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace qce {

namespace {

// Visits every line in [firstLine, lastLine] in order with its indentation in columns, or -1 when it
// holds only whitespace. `visit` returns false to stop. Reads the rope once, skipping line contents
// as soon as the indentation is known.
template <typename Visit>
void scanIndentation(const Rope &rope, qsizetype firstLine, qsizetype lastLine, int tabWidth, Visit visit) {
  const qsizetype lines = rope.lineCount();
  lastLine = qMin(lastLine, lines - 1);
  if (firstLine > lastLine)
    return;
  ChunkIterator it(rope, rope.lineStart(firstLine));
  qsizetype line = firstLine;
  int indent = 0;
  bool inIndent = true;
  QStringView chunk;
  while (it.next(&chunk)) {
    qsizetype i = 0;
    const qsizetype n = chunk.size();
    while (i < n) {
      if (inIndent) {
        const QChar c = chunk[i];
        if (c == u' ') {
          ++indent;
          ++i;
          continue;
        }
        if (c == u'\t') {
          indent += tabWidth - indent % tabWidth;
          ++i;
          continue;
        }
        if (c == u'\r' || c == u'\n') {
          if (c == u'\r') {
            ++i;
            continue;
          }
          // Blank line.
          if (!visit(line, -1) || ++line > lastLine)
            return;
          indent = 0;
          ++i;
          continue;
        }
        inIndent = false;
        if (!visit(line, indent))
          return;
      }
      // Rest of the line: jump to its end.
      const qsizetype nl = chunk.indexOf(u'\n', i);
      if (nl < 0)
        break;
      if (++line > lastLine)
        return;
      indent = 0;
      inIndent = true;
      i = nl + 1;
    }
  }
  // The text ended inside a line that has not been reported (a last line holding only whitespace).
  if (inIndent && line <= lastLine)
    visit(line, -1);
}

} // namespace

std::optional<FoldRange> FoldProvider::rangeAt(const TextSnapshot &text, qsizetype line) {
  const QList<FoldRange> ranges = foldRanges(text, line, line);
  for (const FoldRange &r : ranges)
    if (r.startLine == line)
      return r;
  return std::nullopt;
}

std::optional<FoldRange>
FoldProvider::rangeEnclosing(const TextSnapshot &text, qsizetype line, const FoldMap *folded, qsizetype maxLinesUp) {
  const qsizetype stop = qMax<qsizetype>(0, line - maxLinesUp);
  for (qsizetype h = line; h >= stop; --h) {
    if (folded && folded->isHidden(h))
      continue;
    if (folded && folded->isFolded(h))
      continue;
    if (const auto r = rangeAt(text, h); r && r->endLine >= line)
      return r;
  }
  return std::nullopt;
}

void IndentFoldProvider::setTabWidth(int columns) {
  columns = qMax(1, columns);
  if (columns == m_tabWidth)
    return;
  m_tabWidth = columns;
  m_blocks.clear();
  emit invalidated(AllLines, AllLines);
}

void IndentFoldProvider::setMaxScanLines(qsizetype lines) {
  lines = qMax<qsizetype>(1, lines);
  if (lines == m_maxScan)
    return;
  m_maxScan = lines;
  m_blocks.clear();
  emit invalidated(AllLines, AllLines);
}

QList<FoldRange> IndentFoldProvider::computeBlock(
  const TextSnapshot &text, qsizetype firstLine, qsizetype lastLine, qsizetype scanLimit
) const {
  struct Open {
    qsizetype line;
    int indent;
  };
  QList<Open> stack;
  QList<FoldRange> out;
  qsizetype lastNonBlank = -1;
  const qsizetype textLast = text.lineCount() - 1;
  const qsizetype scanLast = scanLimit < 0 ? textLast : qMin(textLast, lastLine + scanLimit);
  bool stopped = false;
  scanIndentation(text.rope(), firstLine, scanLast, m_tabWidth, [&](qsizetype line, int indent) {
    if (indent < 0)
      return true;
    while (!stack.isEmpty() && stack.last().indent >= indent) {
      if (lastNonBlank > stack.last().line)
        out.append({stack.last().line, lastNonBlank});
      stack.removeLast();
    }
    lastNonBlank = line;
    if (line <= lastLine) {
      stack.append({line, indent});
    } else if (stack.isEmpty()) {
      stopped = true; // nothing left that could still end
      return false;
    }
    return true;
  });
  // At the end of the text everything still open ends at the last non-blank line. When the scan was
  // cut short the ends are unknown and those ranges are not offered.
  if (!stopped && scanLast == textLast) {
    for (const Open &o : std::as_const(stack))
      if (lastNonBlank > o.line)
        out.append({o.line, lastNonBlank});
  }
  std::sort(out.begin(), out.end(), [](const FoldRange &a, const FoldRange &b) { return a.startLine < b.startLine; });
  return out;
}

QList<FoldRange> IndentFoldProvider::foldRanges(const TextSnapshot &text, qsizetype firstLine, qsizetype lastLine) {
  firstLine = qMax<qsizetype>(0, firstLine);
  lastLine = qMin(lastLine, text.lineCount() - 1);
  if (firstLine > lastLine)
    return {};
  if (text.version() != m_cacheVersion) {
    m_blocks.clear();
    m_cacheVersion = text.version();
  }
  // Asking for most of a large text (fold all): one pass without the scan limit.
  if (lastLine - firstLine >= 4 * kBlockLines && lastLine - firstLine + 1 >= text.lineCount() / 2)
    return computeBlock(text, firstLine, lastLine, -1);

  QList<FoldRange> out;
  for (qsizetype b = firstLine / kBlockLines; b <= lastLine / kBlockLines; ++b) {
    auto it = m_blocks.find(b);
    if (it == m_blocks.end()) {
      if (m_blocks.size() > 512)
        m_blocks.clear();
      it = m_blocks
             .emplace(b, computeBlock(text, b * kBlockLines, qMin(b * kBlockLines + kBlockLines - 1, text.lineCount() - 1), m_maxScan))
             .first;
    }
    for (const FoldRange &r : std::as_const(it->second))
      if (r.startLine >= firstLine && r.startLine <= lastLine)
        out.append(r);
  }
  return out;
}

qsizetype foldEndLine(const Rope &rope, qsizetype endRow, qsizetype endColumn) {
  if (endColumn <= 0)
    return endRow - 1;
  const qsizetype start = rope.lineStart(endRow);
  const QString prefix = rope.toString(start, start + qMin(endColumn, rope.lineLength(endRow))).trimmed();
  if (prefix.isEmpty())
    return endRow - 1;
  static const QRegularExpression closer(uR"(^(?:[)\]}>]+[;,]?|</[^<>]*>|`{3,}|~{3,}|\*/)$)"_s);
  return closer.match(prefix).hasMatch() ? endRow - 1 : endRow;
}

QList<int> foldDepths(const QList<FoldRange> &ranges) {
  QList<int> depths;
  depths.reserve(ranges.size());
  QList<qsizetype> ends; // end lines of the ranges that enclose the current one
  for (const FoldRange &r : ranges) {
    while (!ends.isEmpty() && ends.last() < r.startLine)
      ends.removeLast();
    depths.append(int(ends.size()) + 1);
    ends.append(r.endLine);
  }
  return depths;
}

} // namespace qce
