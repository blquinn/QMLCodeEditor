#include "theme.h"

#include <memory>

using namespace Qt::StringLiterals;

namespace qce {

namespace {
QVariantMap style(const char *color, bool bold = false, bool italic = false) {
  return {
    {QStringLiteral("color"), QColor(QLatin1StringView(color))},
    {QStringLiteral("bold"), bold},
    {QStringLiteral("italic"), italic}
  };
}
} // namespace

Theme::Theme(QObject *parent) : QObject(parent) {
  rebuildFormats();
  // Any property change (including MEMBER writes from QML) can affect the cached formats.
  connect(this, &Theme::changed, this, &Theme::rebuildFormats);
}

void Theme::rebuildFormats() {
  for (size_t i = 0; i < size_t(TokenStyle::Count); ++i) {
    QTextCharFormat format;
    format.setForeground(m_foreground);
    const QVariantMap entry = m_tokenStyles.value(tokenStyleName(TokenStyle(i))).toMap();
    if (const QColor color = entry.value("color"_L1).value<QColor>(); color.isValid())
      format.setForeground(color);
    if (entry.value("bold"_L1).toBool())
      format.setFontWeight(QFont::Bold);
    if (entry.value("italic"_L1).toBool())
      format.setFontItalic(true);
    m_formats[i] = format;
  }
}

QTextCharFormat Theme::charFormat(TokenStyle style) const {
  return m_formats[qMin(size_t(style), size_t(TokenStyle::Count) - 1)];
}

QList<QTextLayout::FormatRange> Theme::formatRanges(const QList<HighlightSpan> &spans) const {
  QList<QTextLayout::FormatRange> ranges;
  ranges.reserve(spans.size());
  for (const HighlightSpan &span : spans) {
    if (span.style == TokenStyle::Default || span.length <= 0)
      continue;
    ranges.append({int(span.start), int(span.length), charFormat(span.style)});
  }
  return ranges;
}

QColor Theme::severityColor(int severity) const {
  switch (severity) {
  case 1: return m_diagnosticError;
  case 2: return m_diagnosticWarning;
  case 3: return m_diagnosticInfo;
  case 4: return m_diagnosticHint;
  default: return m_foreground;
  }
}

void Theme::setTokenStyles(const QVariantMap &styles) {
  if (m_tokenStyles == styles)
    return;
  m_tokenStyles = styles;
  emit changed();
}

void Theme::assign(const Theme &o) {
  m_background = o.m_background;
  m_foreground = o.m_foreground;
  m_selection = o.m_selection;
  m_selectionForeground = o.m_selectionForeground;
  m_cursor = o.m_cursor;
  m_currentLine = o.m_currentLine;
  m_whitespace = o.m_whitespace;
  m_gutterBackground = o.m_gutterBackground;
  m_lineNumber = o.m_lineNumber;
  m_currentLineNumber = o.m_currentLineNumber;
  m_changeModified = o.m_changeModified;
  m_changeDeleted = o.m_changeDeleted;
  m_foldMarker = o.m_foldMarker;
  m_foldMarkerHover = o.m_foldMarkerHover;
  m_foldRangeHover = o.m_foldRangeHover;
  m_foldPlaceholder = o.m_foldPlaceholder;
  m_foldPlaceholderText = o.m_foldPlaceholderText;
  m_diagnosticError = o.m_diagnosticError;
  m_diagnosticWarning = o.m_diagnosticWarning;
  m_diagnosticInfo = o.m_diagnosticInfo;
  m_diagnosticHint = o.m_diagnosticHint;
  m_virtualText = o.m_virtualText;
  m_inlayHint = o.m_inlayHint;
  m_inlayHintBackground = o.m_inlayHintBackground;
  m_bracketMatch = o.m_bracketMatch;
  m_searchMatch = o.m_searchMatch;
  m_indentGuide = o.m_indentGuide;
  m_indentGuideActive = o.m_indentGuideActive;
  m_tokenStyles = o.m_tokenStyles;
  emit changed();
}

void Theme::applyPreset(const QString &name) {
  const std::unique_ptr<Theme> preset(
    name.compare("light"_L1, Qt::CaseInsensitive) == 0 ? createLight() : createDark()
  );
  assign(*preset);
}

Theme *Theme::createDark(QObject *parent) {
  auto *t = new Theme(parent);
  t->m_tokenStyles = {
    {"keyword", style("#569cd6")},
    {"string", style("#ce9178")},
    {"comment", style("#6a9955", false, true)},
    {"number", style("#b5cea8")},
    {"type", style("#4ec9b0")},
    {"function", style("#dcdcaa")},
    {"variable", style("#9cdcfe")},
    {"constant", style("#4fc1ff")},
    {"operator", style("#d4d4d4")},
    {"punctuation", style("#d4d4d4")},
    {"preprocessor", style("#c586c0")},
    {"property", style("#9cdcfe")},
    {"attribute", style("#d7ba7d")},
    {"tag", style("#569cd6")},
    {"heading", style("#569cd6", true)},
    {"emphasis", style("#d4d4d4", false, true)},
    {"strong", style("#d4d4d4", true)},
    {"link", style("#3794ff")},
    {"code", style("#ce9178")}
  };
  t->rebuildFormats();
  return t;
}

Theme *Theme::createLight(QObject *parent) {
  auto *t = new Theme(parent);
  t->m_background = QColor(0xff, 0xff, 0xff);
  t->m_foreground = QColor(0x1f, 0x1f, 0x1f);
  t->m_selection = QColor(0xad, 0xd6, 0xff);
  t->m_cursor = QColor(0x00, 0x00, 0x00);
  t->m_currentLine = QColor(0xf3, 0xf3, 0xf3);
  t->m_whitespace = QColor(0xd0, 0xd0, 0xd0);
  t->m_gutterBackground = QColor(0xff, 0xff, 0xff);
  t->m_lineNumber = QColor(0x23, 0x78, 0x93);
  t->m_currentLineNumber = QColor(0x0b, 0x21, 0x6f);
  t->m_changeModified = QColor(0x2e, 0x7d, 0xd1);
  t->m_changeDeleted = QColor(0xc7, 0x2e, 0x2e);
  t->m_foldMarker = QColor(0x80, 0x80, 0x80);
  t->m_foldMarkerHover = QColor(0x20, 0x20, 0x20);
  t->m_foldRangeHover = QColor(0x00, 0x00, 0x00, 0x0d);
  t->m_foldPlaceholder = QColor(0xe0, 0xe0, 0xe0);
  t->m_foldPlaceholderText = QColor(0x55, 0x55, 0x55);
  t->m_diagnosticError = QColor(0xe5, 0x14, 0x00);
  t->m_diagnosticWarning = QColor(0xbf, 0x88, 0x03);
  t->m_diagnosticInfo = QColor(0x1a, 0x85, 0xff);
  t->m_diagnosticHint = QColor(0x6e, 0x6e, 0x6e);
  t->m_virtualText = QColor(0x8a, 0x8a, 0x8a);
  t->m_inlayHint = QColor(0x6a, 0x72, 0x7c);
  t->m_inlayHintBackground = QColor(0xd8, 0xdc, 0xe2, 0x90);
  t->m_bracketMatch = QColor(0xb4, 0xc4, 0xd8, 0xa0);
  t->m_searchMatch = QColor(0xff, 0xd3, 0x3d, 0x90);
  t->m_indentGuide = QColor(0xdc, 0xdc, 0xdc);
  t->m_indentGuideActive = QColor(0x9a, 0x9a, 0x9a);
  t->m_tokenStyles = {
    {"keyword", style("#0000ff")},
    {"string", style("#a31515")},
    {"comment", style("#008000", false, true)},
    {"number", style("#098658")},
    {"type", style("#267f99")},
    {"function", style("#795e26")},
    {"variable", style("#001080")},
    {"constant", style("#0070c1")},
    {"operator", style("#1f1f1f")},
    {"punctuation", style("#1f1f1f")},
    {"preprocessor", style("#af00db")},
    {"property", style("#001080")},
    {"attribute", style("#e50000")},
    {"tag", style("#800000")},
    {"heading", style("#800000", true)},
    {"emphasis", style("#1f1f1f", false, true)},
    {"strong", style("#1f1f1f", true)},
    {"link", style("#0066cc")},
    {"code", style("#a31515")}
  };
  t->rebuildFormats();
  return t;
}

} // namespace qce
