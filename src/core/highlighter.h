#ifndef QCE_HIGHLIGHTER_H
#define QCE_HIGHLIGHTER_H

#include "core/textsnapshot.h"

#include <QtCore/QList>
#include <QtCore/QObject>

namespace qce {

// What kind of token a span is. Colors live in the theme, not here, so highlighters (tree-sitter in
// M6) stay free of presentation. Theme token-style names are these in lower case.
enum class TokenStyle : quint8 {
  Default,
  Keyword,
  String,
  Comment,
  Number,
  Type,
  Function,
  Variable,
  Constant,
  Operator,
  Punctuation,
  Preprocessor,
  Property,
  Attribute,
  Tag,
  Heading,
  Emphasis,
  Strong,
  Link,
  Code,
  Count
};

// Lower-case name of a style ("keyword"), or an empty string for Default and Count.
QLatin1StringView tokenStyleName(TokenStyle style);

// A styled range inside one line: UTF-16 columns, [start, start + length).
struct HighlightSpan {
  qsizetype start = 0;
  qsizetype length = 0;
  TokenStyle style = TokenStyle::Default;
  friend constexpr bool operator==(const HighlightSpan &, const HighlightSpan &) = default;
};

// Produces styled spans for the lines the editor is about to draw. Implementations read the
// snapshot (any thread-safe state they keep is their own business) and must be cheap for a few
// dozen lines: the editor asks on the GUI thread while laying out the viewport.
class TextDocument;

class Highlighter : public QObject {
  Q_OBJECT
public:
  static constexpr qsizetype AllLines = -1;

  explicit Highlighter(QObject *parent = nullptr) : QObject(parent) {}

  // The editor calls attach() when this becomes its highlighter and detach() when it stops being
  // (or the editor goes away). A highlighter that follows edits (a parser) connects to the
  // document's changed/textReset/loadFinished signals here; stateless ones ignore both.
  virtual void attach(TextDocument *) {}
  virtual void detach() {}

  // One entry per line in [firstLine, lastLine] (inclusive, clamped to the document), spans sorted
  // by start and non-overlapping. Unstyled text needs no span.
  virtual QList<QList<HighlightSpan>>
  highlightLines(const TextSnapshot &text, qsizetype firstLine, qsizetype lastLine) = 0;

signals:
  // Spans of these lines (all lines when firstLine == AllLines) may have changed; the editor drops
  // its cached layouts for them.
  void invalidated(qsizetype firstLine, qsizetype lastLine);
};

// Styles nothing.
class NullHighlighter : public Highlighter {
  Q_OBJECT
public:
  using Highlighter::Highlighter;
  QList<QList<HighlightSpan>>
  highlightLines(const TextSnapshot &text, qsizetype firstLine, qsizetype lastLine) override;
};

} // namespace qce

#endif // QCE_HIGHLIGHTER_H
