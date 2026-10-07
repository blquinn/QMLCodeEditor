#ifndef QCE_VIM_VIMREGEX_H
#define QCE_VIM_VIMREGEX_H

#include <QtCore/QRegularExpression>
#include <QtCore/QString>

namespace qce::vim {

struct PatternOptions {
  bool ignoreCase = false;
  bool smartCase = false; // with ignoreCase: a pattern with an uppercase letter is case sensitive
};

struct CompiledPattern {
  QRegularExpression regex;
  // Set when the pattern is plain text, optionally between \< and \>: a search can then use the
  // rope's chunked substring search instead of matching every line with the regex.
  QString literal;
  bool wholeWord = false;
  bool caseSensitive = true;
  QString error; // empty when the pattern compiled
  bool valid() const { return error.isEmpty() && !regex.pattern().isEmpty() && regex.isValid(); }
};

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
