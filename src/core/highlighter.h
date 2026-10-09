#ifndef QCE_HIGHLIGHTER_H
#define QCE_HIGHLIGHTER_H

#include "core/textsnapshot.h"

#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <optional>

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
  Count,
  // Styles a host registers with registerTokenStyle() take the values from here to 255.
  FirstCustom = 64
};
constexpr int kMaxCustomTokenStyles = 256 - int(TokenStyle::FirstCustom);

constexpr bool isCustomTokenStyle(TokenStyle style) { return quint8(style) >= quint8(TokenStyle::FirstCustom); }

// Lower-case name of a style ("keyword"), or an empty string for Default, Count and unregistered values.
QString tokenStyleName(TokenStyle style);

// Host-defined token styles (API-13). A name is lower case and may have dots ("variable.defined"); a
// query capture `@variable.defined` maps to it by the same longest-prefix rule as the built-in names, and
// Theme colors it by that name. Registering is idempotent, thread-safe and permanent; a built-in name
// returns the built-in style. Returns Default when all kMaxCustomTokenStyles slots are taken or the name is
// empty. Register styles before the languages whose queries use them: a query's capture styles are fixed
// when it compiles.
TokenStyle registerTokenStyle(QStringView name);
// The built-in or registered style with this name (case-insensitive).
std::optional<TokenStyle> tokenStyleFromName(QStringView name);
// Names of the registered custom styles, in registration order (style FirstCustom + index).
QStringList customTokenStyleNames();

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

  // The spans of columns [startColumn, endColumn) of one line, relative to startColumn and clipped to the
  // range (PERF-01). The editor asks for this instead of highlightLines() when it lays out only a window of a
  // very long line, so an implementation can avoid styling the whole line. The default slices highlightLines().
  virtual QList<HighlightSpan>
  highlightRange(const TextSnapshot &text, qsizetype line, qsizetype startColumn, qsizetype endColumn);

signals:
  // Spans of these lines (all lines when firstLine == AllLines) may have changed; the editor drops
  // its cached layouts for them.
  void invalidated(qsizetype firstLine, qsizetype lastLine);
};

// One line's spans with `overlay` painted over `base`: base spans are cut around the overlay's, so the
// result is sorted and non-overlapping. Default and empty overlay spans are skipped (they don't erase the
// style underneath). Inputs are sorted by start and non-overlapping, as highlightLines() promises.
QList<HighlightSpan> overlaySpans(const QList<HighlightSpan> &base, const QList<HighlightSpan> &overlay);

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
