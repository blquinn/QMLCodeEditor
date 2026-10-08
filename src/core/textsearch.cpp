#include "core/textsearch.h"

#include <QtCore/QStringMatcher>

#include <limits>

using namespace Qt::StringLiterals;

namespace qce::search {

bool isWordChar(QChar c) { return c.isLetterOrNumber() || c == u'_'; }

namespace {

// Calls fn(start) for every position in [from, end - needle.size()] where the needle matches, in
// ascending order (matches may overlap) until fn returns false.
template <typename Fn>
void forEachMatch(
  const Rope &rope, const QString &needle, qsizetype from, qsizetype end, Options options, Fn fn,
  const std::atomic_bool *cancel = nullptr
) {
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
    if (cancel && cancel->load(std::memory_order_relaxed))
      return;
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
  const Rope &rope, const QRegularExpression &regex, qsizetype start, qsizetype end, qsizetype limit, bool *capped,
  const std::atomic_bool *cancel
) {
  QList<Selection> out;
  if (capped)
    *capped = false;
  if (!regex.isValid() || regex.pattern().isEmpty())
    return out;
  start = qBound<qsizetype>(0, start, rope.length());
  end = qBound<qsizetype>(start, end, rope.length());
  forEachLineMatch(
    rope, regex, rope.lineAt(start), rope.lineAt(end),
    [&](qsizetype base, const QRegularExpressionMatch &m) {
      if (m.capturedLength() == 0)
        return true;
      if (out.size() >= limit) {
        if (capped)
          *capped = true;
        return false;
      }
      out.append({base + m.capturedStart(), base + m.capturedEnd()});
      return true;
    },
    true, cancel
  );
  return out;
}

void forEachLineMatch(
  const Rope &rope, const QRegularExpression &regex, qsizetype firstLine, qsizetype lastLine,
  const std::function<bool(qsizetype, const QRegularExpressionMatch &)> &fn, bool allInLine,
  const std::atomic_bool *cancel
) {
  if (!regex.isValid() || regex.pattern().isEmpty())
    return;
  lastLine = qMin(lastLine, rope.lineCount() - 1);
  for (qsizetype line = qMax<qsizetype>(0, firstLine); line <= lastLine; ++line) {
    if (cancel && cancel->load(std::memory_order_relaxed))
      return;
    const qsizetype base = rope.lineStart(line);
    const QString text = rope.toString(base, rope.lineEnd(line));
    QRegularExpressionMatchIterator it = regex.globalMatch(text);
    while (it.hasNext()) {
      if (!fn(base, it.next()))
        return;
      if (!allInLine)
        break;
    }
  }
}

std::optional<Selection>
find(const Rope &rope, const Pattern &pattern, qsizetype from, bool forward, bool wrap, bool includeAt) {
  if (!pattern.valid())
    return std::nullopt;
  if (forward && !pattern.literal.isEmpty()) {
    from = qBound<qsizetype>(0, from, rope.length());
    if (!includeAt && from < rope.length())
      from += rope.at(from).isHighSurrogate() ? 2 : 1;
    return findNext(rope, pattern.literal, qMin(from, rope.length()), {pattern.caseSensitive, pattern.wholeWord}, wrap);
  }
  return findRegex(rope, pattern.regex, from, forward, wrap, includeAt);
}

QList<Selection> findAll(
  const Rope &rope, const Pattern &pattern, qsizetype start, qsizetype end, qsizetype limit, bool *capped,
  const std::atomic_bool *cancel
) {
  if (!pattern.valid()) {
    if (capped)
      *capped = false;
    return {};
  }
  if (pattern.literal.isEmpty())
    return findAllRegex(rope, pattern.regex, start, end, limit, capped, cancel);
  QList<Selection> out;
  if (capped)
    *capped = false;
  const qsizetype n = pattern.literal.size();
  qsizetype lastEnd = 0;
  forEachMatch(
    rope, pattern.literal, start, end, {pattern.caseSensitive, pattern.wholeWord},
    [&](qsizetype at) {
      if (at < lastEnd)
        return true;
      if (out.size() >= limit) {
        if (capped)
          *capped = true;
        return false;
      }
      out.append({at, at + n});
      lastEnd = at + n;
      return true;
    },
    cancel
  );
  return out;
}

Pattern compileQuery(const QString &text, Query query) {
  Pattern result;
  if (text.isEmpty())
    return result;
  result.caseSensitive = query.caseSensitive;
  result.wholeWord = query.wholeWord;
  QString source = query.regex ? text : QRegularExpression::escape(text);
  if (query.wholeWord)
    source = u"(?<![\\p{L}\\p{N}_])(?:"_s + source + u")(?![\\p{L}\\p{N}_])"_s;
  QRegularExpression::PatternOptions options = QRegularExpression::UseUnicodePropertiesOption;
  if (!query.caseSensitive)
    options |= QRegularExpression::CaseInsensitiveOption;
  result.regex = QRegularExpression(source, options);
  if (!result.regex.isValid())
    result.error = result.regex.errorString();
  else if (!query.regex)
    result.literal = text;
  return result;
}

QString expandReplacement(const QString &rep, const QRegularExpressionMatch &match) {
  QString out;
  const int groups = match.regularExpression().captureCount();
  for (qsizetype i = 0; i < rep.size(); ++i) {
    const QChar c = rep[i];
    if (c == u'$' && i + 1 < rep.size()) {
      const QChar d = rep[i + 1];
      if (d == u'$') {
        out += u'$';
        ++i;
      } else if (d == u'&') {
        out += match.captured(0);
        ++i;
      } else if (d.isDigit() && d.unicode() < 128) {
        int index = d.digitValue();
        qsizetype used = 1;
        if (i + 2 < rep.size() && rep[i + 2].isDigit() && rep[i + 2].unicode() < 128) {
          const int two = index * 10 + rep[i + 2].digitValue();
          if (two <= groups) {
            index = two;
            used = 2;
          }
        }
        if (index <= groups) {
          out += match.captured(index);
          i += used;
        } else {
          out += c;
        }
      } else {
        out += c;
      }
    } else if (c == u'\\' && i + 1 < rep.size()) {
      const QChar e = rep[i + 1];
      if (e == u'n') {
        out += u'\n';
        ++i;
      } else if (e == u't') {
        out += u'\t';
        ++i;
      } else if (e == u'\\') {
        out += u'\\';
        ++i;
      } else {
        out += c;
      }
    } else {
      out += c;
    }
  }
  return out;
}

} // namespace qce::search
