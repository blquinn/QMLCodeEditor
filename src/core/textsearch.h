#ifndef QCE_TEXTSEARCH_H
#define QCE_TEXTSEARCH_H

#include "core/rope.h"
#include "core/selection.h"

#include <QtCore/QRegularExpression>

#include <optional>

namespace qce::search {

struct Options {
  bool caseSensitive = true;
  bool wholeWord = false; // the match must not touch word characters on either side
};

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
// whether there were more). Empty matches are skipped.
QList<Selection> findAllRegex(
  const Rope &rope, const QRegularExpression &regex, qsizetype start, qsizetype end, qsizetype limit,
  bool *capped = nullptr
);

// True for the characters that make up words here: letters, digits and underscore.
bool isWordChar(QChar c);

} // namespace qce::search

#endif // QCE_TEXTSEARCH_H
