#include "theme.h"

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

Theme::Theme(QObject *parent) : QObject(parent) {}

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
  m_tokenStyles = o.m_tokenStyles;
  emit changed();
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
    {"preprocessor", style("#c586c0")}
  };
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
    {"preprocessor", style("#af00db")}
  };
  return t;
}

} // namespace qce
