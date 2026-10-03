#ifndef QCE_TEXTSEARCH_H
#define QCE_TEXTSEARCH_H

#include "core/rope.h"
#include "core/selection.h"

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

// True for the characters that make up words here: letters, digits and underscore.
bool isWordChar(QChar c);

} // namespace qce::search

#endif // QCE_TEXTSEARCH_H
