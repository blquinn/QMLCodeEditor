#ifndef QCE_FOLDING_H
#define QCE_FOLDING_H

#include "core/displaymap.h"
#include "core/foldprovider.h"

namespace qce::folding {

// Fold commands over a display map and a provider (FOLD-07). Each returns whether any fold changed.

// Folds the range starting on `line`, or unfolds it when it is folded (the gutter's click).
bool toggle(DisplayMap &map, FoldProvider &provider, const TextSnapshot &text, qsizetype line);
// Folds the innermost range around `line` that is not folded yet.
bool foldAt(DisplayMap &map, FoldProvider &provider, const TextSnapshot &text, qsizetype line);
// Unfolds the fold whose header is `line`; failing that, the innermost fold around it.
bool unfoldAt(DisplayMap &map, qsizetype line);
bool foldAll(DisplayMap &map, FoldProvider &provider, const TextSnapshot &text);
bool unfoldAll(DisplayMap &map);
// Replaces the folds by those of nesting level `level` (1 = outermost).
bool foldToLevel(DisplayMap &map, FoldProvider &provider, const TextSnapshot &text, int level);

} // namespace qce::folding

#endif // QCE_FOLDING_H
