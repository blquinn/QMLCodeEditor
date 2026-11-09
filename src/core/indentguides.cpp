#include "core/indentguides.h"

#include "core/rope.h"

#include <QtCore/QString>

#include <algorithm>

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
  if (std::none_of(raw.cbegin(), raw.cend(), [](int indent) { return indent < 0; }))
    return result;

  // Each blank line takes the indent of the nearest text line above and below it, at most `maxSearch` lines
  // away (0 where there is none). Looking that up line by line rescans a blank stretch once per line in it, which
  // is quadratic in the stretch (a view of a mostly blank file); two sweeps find every answer in one visit. Each
  // sweep starts from the nearest text line outside the range, looked for once.
  const qsizetype count = lastLine - firstLine + 1;
  QList<int> above(count, 0);
  {
    qsizetype textLine = -1; // the nearest text line seen so far, or -1
    int textIndent = 0;
    for (qsizetype l = firstLine - 1, n = 0; l >= 0 && n < maxSearch; --l, ++n) {
      if (const int indent = lineIndent(rope, l, tabWidth); indent >= 0) {
        textLine = l;
        textIndent = indent;
        break;
      }
    }
    for (qsizetype line = firstLine; line <= lastLine; ++line) {
      const int indent = raw[line - firstLine];
      if (indent >= 0) {
        textLine = line;
        textIndent = indent;
      } else if (textLine >= 0 && line - textLine <= maxSearch) {
        above[line - firstLine] = textIndent;
      }
    }
  }
  {
    qsizetype textLine = -1;
    int textIndent = 0;
    for (qsizetype l = lastLine + 1, n = 0; l < lineCount && n < maxSearch; ++l, ++n) {
      if (const int indent = lineIndent(rope, l, tabWidth); indent >= 0) {
        textLine = l;
        textIndent = indent;
        break;
      }
    }
    for (qsizetype line = lastLine; line >= firstLine; --line) {
      const int indent = raw[line - firstLine];
      if (indent >= 0) {
        textLine = line;
        textIndent = indent;
        continue;
      }
      const int a = above[line - firstLine];
      const int b = textLine >= 0 && textLine - line <= maxSearch ? textIndent : 0;
      result[line - firstLine] = a == b ? a : qMin(qMin(a, b) + unit, qMax(a, b));
    }
  }
  return result;
}

} // namespace qce
