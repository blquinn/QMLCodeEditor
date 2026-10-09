#include "core/highlighter.h"

#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>

#include <vector>

using namespace Qt::StringLiterals;

namespace qce {

namespace {
QLatin1StringView builtinName(TokenStyle style) {
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
  case TokenStyle::FirstCustom:
    break;
  }
  return ""_L1;
}

QMutex &customMutex() {
  static QMutex mutex;
  return mutex;
}
std::vector<QString> &customNames() {
  static std::vector<QString> names;
  return names;
}
} // namespace

QString tokenStyleName(TokenStyle style) {
  if (isCustomTokenStyle(style)) {
    const QMutexLocker lock(&customMutex());
    const size_t index = size_t(quint8(style) - quint8(TokenStyle::FirstCustom));
    return index < customNames().size() ? customNames()[index] : QString();
  }
  return QString(builtinName(style));
}

std::optional<TokenStyle> tokenStyleFromName(QStringView name) {
  const QString key = name.toString().trimmed().toLower();
  if (key.isEmpty())
    return std::nullopt;
  for (int i = 1; i < int(TokenStyle::Count); ++i)
    if (builtinName(TokenStyle(i)) == key)
      return TokenStyle(i);
  const QMutexLocker lock(&customMutex());
  const auto &names = customNames();
  for (size_t i = 0; i < names.size(); ++i)
    if (names[i] == key)
      return TokenStyle(int(TokenStyle::FirstCustom) + int(i));
  return std::nullopt;
}

TokenStyle registerTokenStyle(QStringView name) {
  if (const auto existing = tokenStyleFromName(name))
    return *existing;
  const QString key = name.toString().trimmed().toLower();
  if (key.isEmpty())
    return TokenStyle::Default;
  const QMutexLocker lock(&customMutex());
  auto &names = customNames();
  for (size_t i = 0; i < names.size(); ++i) // registered by another thread since the lookup above
    if (names[i] == key)
      return TokenStyle(int(TokenStyle::FirstCustom) + int(i));
  if (names.size() >= size_t(kMaxCustomTokenStyles))
    return TokenStyle::Default;
  names.push_back(key);
  return TokenStyle(int(TokenStyle::FirstCustom) + int(names.size()) - 1);
}

QStringList customTokenStyleNames() {
  const QMutexLocker lock(&customMutex());
  QStringList out;
  for (const QString &name : customNames())
    out.append(name);
  return out;
}

QList<HighlightSpan>
Highlighter::highlightRange(const TextSnapshot &text, qsizetype line, qsizetype startColumn, qsizetype endColumn) {
  QList<HighlightSpan> out;
  const QList<QList<HighlightSpan>> lines = highlightLines(text, line, line);
  if (lines.isEmpty())
    return out;
  for (HighlightSpan span : lines.first()) {
    const qsizetype from = qMax(span.start, startColumn), to = qMin(span.start + span.length, endColumn);
    if (to <= from)
      continue;
    span.start = from - startColumn;
    span.length = to - from;
    out.append(span);
  }
  return out;
}

QList<HighlightSpan> overlaySpans(const QList<HighlightSpan> &base, const QList<HighlightSpan> &overlay) {
  if (overlay.isEmpty())
    return base;
  QList<HighlightSpan> out;
  out.reserve(base.size() + overlay.size() * 2);
  // Base spans after the last overlay span handled; the head of one that an overlay cuts is trimmed in place.
  QList<HighlightSpan> rest = base;
  qsizetype i = 0;
  for (const HighlightSpan &o : overlay) {
    if (o.style == TokenStyle::Default || o.length <= 0)
      continue;
    const qsizetype oEnd = o.start + o.length;
    while (i < rest.size() && rest[i].start + rest[i].length <= o.start) // wholly before the overlay span
      out.append(rest[i++]);
    if (i < rest.size() && rest[i].start < o.start) // straddles its start: keep the head
      out.append({rest[i].start, o.start - rest[i].start, rest[i].style});
    out.append(o);
    while (i < rest.size() && rest[i].start + rest[i].length <= oEnd) // swallowed
      ++i;
    if (i < rest.size() && rest[i].start < oEnd) { // straddles its end: keep the tail
      const qsizetype end = rest[i].start + rest[i].length;
      rest[i] = {oEnd, end - oEnd, rest[i].style};
    }
  }
  while (i < rest.size())
    out.append(rest[i++]);
  return out;
}

QList<QList<HighlightSpan>>
NullHighlighter::highlightLines(const TextSnapshot &text, qsizetype firstLine, qsizetype lastLine) {
  firstLine = qMax<qsizetype>(firstLine, 0);
  lastLine = qMin(lastLine, text.lineCount() - 1);
  return QList<QList<HighlightSpan>>(qMax<qsizetype>(0, lastLine - firstLine + 1));
}

} // namespace qce
