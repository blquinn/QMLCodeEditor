#ifndef QCE_INDENTATION_H
#define QCE_INDENTATION_H

#include "core/rope.h"

#include <optional>

namespace qce {

struct IndentationGuess {
  bool insertSpaces = true;
  int indentWidth = 0; // 0: no width could be told (tab-indented, or no indent steps seen)
};

// Guesses how a document is indented from its first `maxLines` lines (one linear pass, so the cost
// is bounded on huge files). Returns nothing when no line is indented.
std::optional<IndentationGuess> detectIndentation(const Rope &rope, qsizetype maxLines = 10000);

} // namespace qce

#endif // QCE_INDENTATION_H
