#include "core/indentguides.h"

#include "core/rope.h"

#include <QtCore/QString>

namespace qce {
namespace {

// Indent of buffer line `line`, reading only its leading whitespace (a huge line costs a prefix).
int lineIndent(const Rope &rope, qsizetype line, int tabWidth) {
  constexpr qsizetype kChunk = 256;
  const qsizetype end = rope.lineEnd(line);
  int column = 0;
  for (qsizetype start = rope.lineStart(line); start < end;) {
    const QString piece = rope.toString(start, qMin(end, start + kChunk));
    for (const QChar ch : piece) {
      if (ch == u' ')
        ++column;
      else if (ch == u'\t')
        column += tabWidth - column % tabWidth;
      else
        return column;
    }
    start += piece.size();
  }
  return -1;
}

} // namespace

int indentColumns(QStringView text, int tabWidth) {
  tabWidth = qMax(1, tabWidth);
  int column = 0;
  for (const QChar ch : text) {
    if (ch == u' ')
      ++column;
    else if (ch == u'\t')
      column += tabWidth - column % tabWidth;
    else
      return column;
  }
  return -1;
}

QList<int> effectiveIndents(
  const Rope &rope, qsizetype firstLine, qsizetype lastLine, int tabWidth, int unit, qsizetype maxSearch
) {
  tabWidth = qMax(1, tabWidth);
  const qsizetype lineCount = rope.lineCount();
  firstLine = qMax<qsizetype>(0, firstLine);
  lastLine = qMin(lastLine, lineCount - 1);
  QList<int> result;
  if (lastLine < firstLine)
    return result;
  result.reserve(lastLine - firstLine + 1);
  for (qsizetype line = firstLine; line <= lastLine; ++line)
    result.append(lineIndent(rope, line, tabWidth));
  // Blank lines are resolved against the lines' own indents, so these read `raw`, not `result`.
  const QList<int> raw = result;

  // Indent of the nearest text line before / after `line`, looking in `raw` and then outside it.
  auto above = [&](qsizetype line) {
    for (qsizetype l = line - 1, n = 0; l >= 0 && n < maxSearch; --l, ++n) {
      const int indent = l >= firstLine ? raw[l - firstLine] : lineIndent(rope, l, tabWidth);
      if (indent >= 0)
        return indent;
    }
    return 0;
  };
  auto below = [&](qsizetype line) {
    for (qsizetype l = line + 1, n = 0; l < lineCount && n < maxSearch; ++l, ++n) {
      const int indent = l <= lastLine ? raw[l - firstLine] : lineIndent(rope, l, tabWidth);
      if (indent >= 0)
        return indent;
    }
    return 0;
  };
  for (qsizetype line = firstLine; line <= lastLine; ++line) {
    if (raw[line - firstLine] >= 0)
      continue;
    const int a = above(line), b = below(line);
    result[line - firstLine] = a == b ? a : qMin(qMin(a, b) + unit, qMax(a, b));
  }
  return result;
}

} // namespace qce
