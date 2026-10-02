#include "core/highlighter.h"

using namespace Qt::StringLiterals;

namespace qce {

QLatin1StringView tokenStyleName(TokenStyle style) {
  switch (style) {
  case TokenStyle::Keyword:
    return "keyword"_L1;
  case TokenStyle::String:
    return "string"_L1;
  case TokenStyle::Comment:
    return "comment"_L1;
  case TokenStyle::Number:
    return "number"_L1;
  case TokenStyle::Type:
    return "type"_L1;
  case TokenStyle::Function:
    return "function"_L1;
  case TokenStyle::Variable:
    return "variable"_L1;
  case TokenStyle::Constant:
    return "constant"_L1;
  case TokenStyle::Operator:
    return "operator"_L1;
  case TokenStyle::Punctuation:
    return "punctuation"_L1;
  case TokenStyle::Preprocessor:
    return "preprocessor"_L1;
  case TokenStyle::Property:
    return "property"_L1;
  case TokenStyle::Attribute:
    return "attribute"_L1;
  case TokenStyle::Tag:
    return "tag"_L1;
  case TokenStyle::Heading:
    return "heading"_L1;
  case TokenStyle::Emphasis:
    return "emphasis"_L1;
  case TokenStyle::Strong:
    return "strong"_L1;
  case TokenStyle::Link:
    return "link"_L1;
  case TokenStyle::Code:
    return "code"_L1;
  case TokenStyle::Default:
  case TokenStyle::Count:
    break;
  }
  return ""_L1;
}

QList<QList<HighlightSpan>>
NullHighlighter::highlightLines(const TextSnapshot &text, qsizetype firstLine, qsizetype lastLine) {
  firstLine = qMax<qsizetype>(firstLine, 0);
  lastLine = qMin(lastLine, text.lineCount() - 1);
  return QList<QList<HighlightSpan>>(qMax<qsizetype>(0, lastLine - firstLine + 1));
}

} // namespace qce
