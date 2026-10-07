#include "core/textsearch.h"

#include <QtCore/QStringMatcher>

#include <limits>

namespace qce::search {

bool isWordChar(QChar c) { return c.isLetterOrNumber() || c == u'_'; }

namespace {

// Calls fn(start) for every position in [from, end - needle.size()] where the needle matches, in
// ascending order (matches may overlap) until fn returns false.
template <typename Fn>
void forEachMatch(const Rope &rope, const QString &needle, qsizetype from, qsizetype end, Options options, Fn fn) {
  const qsizetype n = needle.size();
  end = qMin(end, rope.length());
  if (n == 0 || end - from < n)
    return;
  const QStringMatcher matcher(needle, options.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive);
  auto accepted = [&](qsizetype start) {
    if (options.wholeWord) {
      if (start > 0 && isWordChar(rope.at(start - 1)))
        return true; // rejected, keep going
      if (start + n < rope.length() && isWordChar(rope.at(start + n)))
        return true;
    }
    return fn(start);
  };

  from = rope.snapToCodePoint(from);
  ChunkIterator it(rope, from);
  QString carry; // the last n - 1 units before the current chunk
  qsizetype base = from;
  QStringView chunk;
  while (base < end && it.next(&chunk)) {
    if (base + chunk.size() > end)
      chunk = chunk.left(end - base);
    // Matches that start in the carry and end in this chunk.
    if (!carry.isEmpty()) {
      const QString joined = carry + chunk.left(n - 1);
      const qsizetype carryStart = base - carry.size();
      for (qsizetype pos = matcher.indexIn(joined, 0); pos >= 0 && pos < carry.size();
           pos = matcher.indexIn(joined, pos + 1)) {
        if (!accepted(carryStart + pos))
          return;
      }
    }
    // Matches that start in this chunk.
    for (qsizetype pos = matcher.indexIn(chunk, 0); pos >= 0; pos = matcher.indexIn(chunk, pos + 1)) {
      if (!accepted(base + pos))
        return;
    }
    if (chunk.size() >= n - 1)
      carry = chunk.right(n - 1).toString();
    else
      carry = (carry + chunk).right(n - 1);
    base += chunk.size();
  }
}

} // namespace

std::optional<Selection>
findNext(const Rope &rope, const QString &needle, qsizetype from, Options options, bool wrap) {
  const qsizetype n = needle.size();
  if (n == 0)
    return std::nullopt;
  from = qBound<qsizetype>(0, from, rope.length());
  std::optional<Selection> found;
  auto first = [&](qsizetype start) {
    found = Selection{start, start + n};
    return false;
  };
  forEachMatch(rope, needle, from, rope.length(), options, first);
  if (!found && wrap && from > 0)
    forEachMatch(rope, needle, 0, from + n - 1, options, first);
  return found;
}

QList<Selection> findAll(const Rope &rope, const QString &needle, qsizetype limit, Options options, bool *capped) {
  QList<Selection> out;
  if (capped)
    *capped = false;
  const qsizetype n = needle.size();
  qsizetype lastEnd = 0;
  forEachMatch(rope, needle, 0, rope.length(), options, [&](qsizetype start) {
    if (start < lastEnd)
      return true; // overlaps the previous match
    if (out.size() >= limit) {
      if (capped)
        *capped = true;
      return false;
    }
    out.append({start, start + n});
    lastEnd = start + n;
    return true;
  });
  return out;
}

} // namespace qce::search

namespace qce::search {

namespace {

// First/last match of `regex` in `text` that starts at or after `fromCol`, or (backward) before it.
bool matchInLine(const QRegularExpression &regex, const QString &text, qsizetype fromCol, bool forward, qsizetype *start, qsizetype *end) {
  if (forward) {
    if (fromCol > text.size())
      return false;
    const QRegularExpressionMatch m = regex.match(text, fromCol);
    if (!m.hasMatch())
      return false;
    *start = m.capturedStart();
    *end = m.capturedEnd();
    return true;
  }
  bool found = false;
  QRegularExpressionMatchIterator it = regex.globalMatch(text);
  while (it.hasNext()) {
    const QRegularExpressionMatch m = it.next();
    if (m.capturedStart() >= fromCol)
      break;
    *start = m.capturedStart();
    *end = m.capturedEnd();
    found = true;
  }
  return found;
}

} // namespace

std::optional<Selection>
findRegex(const Rope &rope, const QRegularExpression &regex, qsizetype from, bool forward, bool wrap, bool includeAt) {
  if (!regex.isValid() || regex.pattern().isEmpty())
    return std::nullopt;
  from = qBound<qsizetype>(0, from, rope.length());
  const qsizetype lines = rope.lineCount();
  const qsizetype line0 = rope.lineAt(from);
  const qsizetype col0 = from - rope.lineStart(line0);
  qsizetype s = 0, e = 0;
  auto lineText = [&](qsizetype line) { return rope.toString(rope.lineStart(line), rope.lineEnd(line)); };
  auto result = [&](qsizetype line) -> std::optional<Selection> {
    const qsizetype base = rope.lineStart(line);
    return Selection{base + s, base + e};
  };

  if (forward) {
    // The rest of the first line (after the cursor's character unless includeAt).
    qsizetype startCol = col0;
    if (!includeAt && from < rope.length() && col0 < rope.lineLength(line0))
      startCol = col0 + (rope.at(from).isHighSurrogate() ? 2 : 1);
    else if (!includeAt)
      startCol = rope.lineLength(line0) + 1; // at the line end: nothing more on this line
    if (matchInLine(regex, lineText(line0), startCol, true, &s, &e))
      return result(line0);
    for (qsizetype line = line0 + 1; line < lines; ++line)
      if (matchInLine(regex, lineText(line), 0, true, &s, &e))
        return result(line);
    if (!wrap)
      return std::nullopt;
    for (qsizetype line = 0; line <= line0; ++line) {
      if (matchInLine(regex, lineText(line), 0, true, &s, &e)) {
        if (line < line0 || s <= col0)
          return result(line);
        break;
      }
    }
    return std::nullopt;
  }

  const qsizetype limitCol = includeAt ? col0 + 1 : col0;
  if (matchInLine(regex, lineText(line0), limitCol, false, &s, &e))
    return result(line0);
  for (qsizetype line = line0 - 1; line >= 0; --line)
    if (matchInLine(regex, lineText(line), std::numeric_limits<qsizetype>::max(), false, &s, &e))
      return result(line);
  if (!wrap)
    return std::nullopt;
  for (qsizetype line = lines - 1; line >= line0; --line) {
    if (matchInLine(regex, lineText(line), std::numeric_limits<qsizetype>::max(), false, &s, &e)) {
      if (line > line0 || s >= col0)
        return result(line);
      break;
    }
  }
  return std::nullopt;
}

QList<Selection> findAllRegex(
  const Rope &rope, const QRegularExpression &regex, qsizetype start, qsizetype end, qsizetype limit, bool *capped
) {
  QList<Selection> out;
  if (capped)
    *capped = false;
  if (!regex.isValid() || regex.pattern().isEmpty())
    return out;
  start = qBound<qsizetype>(0, start, rope.length());
  end = qBound<qsizetype>(start, end, rope.length());
  const qsizetype lastLine = rope.lineAt(end);
  for (qsizetype line = rope.lineAt(start); line <= lastLine; ++line) {
    const qsizetype base = rope.lineStart(line);
    const QString text = rope.toString(base, rope.lineEnd(line));
    QRegularExpressionMatchIterator it = regex.globalMatch(text);
    while (it.hasNext()) {
      const QRegularExpressionMatch m = it.next();
      if (m.capturedLength() == 0)
        continue;
      if (out.size() >= limit) {
        if (capped)
          *capped = true;
        return out;
      }
      out.append({base + m.capturedStart(), base + m.capturedEnd()});
    }
  }
  return out;
}

} // namespace qce::search
