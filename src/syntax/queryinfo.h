#ifndef QCE_QUERYINFO_H
#define QCE_QUERYINFO_H

#include "core/highlighter.h"
#include "core/rope.h"
#include "syntax/ropeinput.h"

#include <QtCore/QRegularExpression>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <optional>
#include <vector>

#include <tree_sitter/api.h>

namespace qce {

// Maps a capture name (@keyword.function) to a token style by its longest dotted prefix
// (SYNTAX-06). nullopt means "not a styling capture" (@spell, @injection.content, unknown names):
// the capture is ignored. TokenStyle::Default is an explicit reset (@none): it hides outer styles.
std::optional<TokenStyle> styleForCapture(QStringView name);

// A text predicate of a pattern: #eq?, #not-eq?, #match?, #not-match?, #any-of?, #not-any-of?
// (and the vim-/lua- spellings). Predicates the engine doesn't know are ignored.
struct QueryPredicate {
  enum class Kind { Eq, NotEq, Match, NotMatch, AnyOf, NotAnyOf };
  Kind kind = Kind::Eq;
  uint32_t capture = 0;
  bool rhsIsCapture = false;
  uint32_t rhsCapture = 0;
  QString literal;       // Eq / NotEq
  QStringList literals;  // AnyOf / NotAnyOf
  QRegularExpression regex; // Match / NotMatch
};

struct PatternInfo {
  std::vector<QueryPredicate> predicates;
  QString injectionLanguage; // #set! injection.language "x"
  bool combined = false;     // #set! injection.combined
};

// Everything derived from a compiled query that the matcher needs per capture and per pattern.
struct QueryInfo {
  std::vector<std::optional<TokenStyle>> captureStyles; // by capture id
  std::vector<uint8_t> captureDepth;                    // dotted segments of the name: @string.special.key = 3
  std::vector<PatternInfo> patterns;                    // by pattern index
  int injectionContent = -1;                            // capture ids, -1 if absent
  int injectionLanguage = -1;

  static QueryInfo analyze(const TSQuery *query);
};

// Text of a node (UTF-16 units from the rope), cut to `maxUnits`.
QString nodeText(const Rope &rope, TSNode node, qsizetype maxUnits = 1024);

// True when every text predicate of the match's pattern holds. `captureNode` finds the node a
// capture id is bound to within the match.
bool predicatesHold(const QueryInfo &info, const TSQueryMatch &match, const Rope &rope);

} // namespace qce

#endif // QCE_QUERYINFO_H
