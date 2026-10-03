#include "core/textsearch.h"

#include <QtCore/QStringMatcher>

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
