#ifndef QCE_TEXTSEARCH_H
#define QCE_TEXTSEARCH_H

#include "core/rope.h"
#include "core/selection.h"

#include <QtCore/QRegularExpression>

#include <atomic>
#include <functional>
#include <optional>

namespace qce::search {

struct Options {
  bool caseSensitive = true;
  bool wholeWord = false; // the match must not touch word characters on either side
};

// A search pattern ready to run, shared by vim's / and :s and by find/replace. `regex` always
// matches what the pattern means, one line at a time. When the pattern is plain text `literal` is
// set too (with `caseSensitive` and `wholeWord`): searches then use the rope's chunked substring
// search instead of matching every line with the regex.
struct Pattern {
  QRegularExpression regex;
  QString literal;
  bool wholeWord = false;
  bool caseSensitive = true;
  QString error; // empty when the pattern compiled
  bool valid() const { return error.isEmpty() && !regex.pattern().isEmpty() && regex.isValid(); }
};

// What a find box holds: `text` is plain text, or a regular expression (QRegularExpression syntax)
// with `regex`.
struct Query {
  bool regex = false;
  bool caseSensitive = false;
  bool wholeWord = false;
};

// Compiles find-box text. Empty text and an invalid expression give a pattern that is not valid()
// (the latter with `error` set).
Pattern compileQuery(const QString &text, Query query);

// Literal search over a rope, chunk by chunk: no whole-document string is ever built, and a match
// may straddle chunk boundaries. Offsets are UTF-16 units.

// The first match that starts at or after `from`. With `wrap`, a miss continues from the start of
// the document (a match starting before `from`). The result's anchor is the start, head the end.
std::optional<Selection>
findNext(const Rope &rope, const QString &needle, qsizetype from, Options options = {}, bool wrap = false);

// Every non-overlapping match in ascending order, at most `limit` of them; `*capped` says whether
// there were more.
QList<Selection>
findAll(const Rope &rope, const QString &needle, qsizetype limit, Options options = {}, bool *capped = nullptr);

// Regular-expression search, line by line: the rope is never turned into one string and a match
// never spans lines (the pattern is applied to each line's text). Offsets are UTF-16 units.

// The nearest match after `from` (strictly: a match starting at `from` does not count unless
// `includeAt`), or, with `forward` false, the nearest one starting before it. With `wrap` a miss
// continues from the other end of the document, and may return a match at or before `from`.
std::optional<Selection> findRegex(
  const Rope &rope, const QRegularExpression &regex, qsizetype from, bool forward = true, bool wrap = true,
  bool includeAt = false
);

// Matches that start in the lines of [start, end), ascending, at most `limit` of them (`*capped` says
// whether there were more). Empty matches are skipped. A set `cancel` is polled once per line and
// ends the search early (the result is then partial).
QList<Selection> findAllRegex(
  const Rope &rope, const QRegularExpression &regex, qsizetype start, qsizetype end, qsizetype limit,
  bool *capped = nullptr, const std::atomic_bool *cancel = nullptr
);

// Calls fn(lineStart, match) for each match of `regex` in the lines [firstLine, lastLine] (empty
// matches included; only the first of each line unless `allInLine`) until fn returns false.
// `match` is relative to the line's text. A set `cancel` is polled once per line.
void forEachLineMatch(
  const Rope &rope, const QRegularExpression &regex, qsizetype firstLine, qsizetype lastLine,
  const std::function<bool(qsizetype lineStart, const QRegularExpressionMatch &match)> &fn,
  bool allInLine = true, const std::atomic_bool *cancel = nullptr
);

// The two engines behind one call, chosen by the pattern.

// findNext's and findRegex's meaning of `from`, `forward`, `wrap` and `includeAt` (a code point
// is stepped over when a match at `from` does not count). The result is start to end.
std::optional<Selection>
find(const Rope &rope, const Pattern &pattern, qsizetype from, bool forward = true, bool wrap = true, bool includeAt = false);

// Matches in [start, end) ascending, at most `limit` (`*capped` says whether there were more).
// Plain-text matches lie inside the range; expression matches start in the lines it touches and
// empty ones are skipped. A set `cancel` ends the search early (the result is then partial).
QList<Selection> findAll(
  const Rope &rope, const Pattern &pattern, qsizetype start, qsizetype end, qsizetype limit, bool *capped = nullptr,
  const std::atomic_bool *cancel = nullptr
);

// Expands a replacement for one match: $1..$99 groups (a group the pattern does not have stays as
// written), $& the whole match, $$ a dollar, \n a line break, \t a tab, \\ a backslash.
QString expandReplacement(const QString &replacement, const QRegularExpressionMatch &match);

// True for the characters that make up words here: letters, digits and underscore.
bool isWordChar(QChar c);

} // namespace qce::search

#endif // QCE_TEXTSEARCH_H
