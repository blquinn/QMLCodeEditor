#ifndef QCE_VIM_VIMREGEX_H
#define QCE_VIM_VIMREGEX_H

#include "core/textsearch.h"

#include <QtCore/QRegularExpression>
#include <QtCore/QString>

namespace qce::vim {

struct PatternOptions {
  bool ignoreCase = false;
  bool smartCase = false; // with ignoreCase: a pattern with an uppercase letter is case sensitive
};

// Vim patterns compile to the pattern type find/replace shares (core/textsearch.h): a regular
// expression, plus the plain text when there is nothing special in it.
using CompiledPattern = search::Pattern;

// Translates a vim pattern to a regular expression. Understands the default "magic" syntax and \v
// (very magic), \m, \M and \V, the multis * \+ \? \= \{n,m} (and the lazy \{-n,m}), groups \( \) \%(,
// alternation \|, \< and \>, \zs, character classes \s \d \w \a \l \u \h (and their negations where
// vim has them), [] collections, ^ and $ only as anchors at the edges of a branch, \1-\9, and \c / \C.
// Look-around (\@=) and multi-line matches (\n inside a pattern) are not supported: the pattern is
// reported invalid. Matches are searched line by line.
CompiledPattern compilePattern(const QString &pattern, PatternOptions options = {});

// Expands the replacement of :s for one match: & and \0 the match, \1-\9 groups, \r and \n a line
// break, \t a tab, \\ \/ \& literal, \u \U \l \L \e \E case changes.
QString expandReplacement(const QString &replacement, const QRegularExpressionMatch &match);

} // namespace qce::vim

#endif // QCE_VIM_VIMREGEX_H
