#ifndef QCE_TEXTMETRICS_H
#define QCE_TEXTMETRICS_H

#include <QtCore/QStringView>
#include <QtGui/QFont>

namespace qce {

// Font metrics for the editor, with a cell-grid fast path (ADR 0001): in a monospace font a line
// made only of printable ASCII and tabs has x = cell * advance, so positions need no shaping.
// Anything else (CJK, emoji, combining marks, proportional fonts) must be measured through the
// line's QTextLayout; isSimple() says which case applies.
class TextMetrics {
public:
  TextMetrics() { update(); }
  explicit TextMetrics(const QFont &font, int tabWidth = 4) : m_font(font), m_tabWidth(tabWidth) { update(); }

  // The system fixed font, or when that is not really monospace (no fontconfig alias, minimal
  // containers) the first fixed-pitch family the platform offers.
  static QFont defaultMonospaceFont();

  void setFont(const QFont &font);
  void setTabWidth(int columns);

  const QFont &font() const { return m_font; }
  // The font to hand to QTextLayout: the same family and size with ligatures and kerning off, so
  // shaped advances equal the cell grid.
  const QFont &layoutFont() const { return m_layoutFont; }
  int tabWidth() const { return m_tabWidth; }

  qreal lineHeight() const { return m_lineHeight; }
  qreal ascent() const { return m_ascent; }
  qreal cellAdvance() const { return m_advance; }
  bool isMonospace() const { return m_monospace; }

  // True when xForColumn()/columnForX() are exact for `line`.
  bool isSimple(QStringView line) const;

  // Cell index of the UTF-16 column (tabs advance to the next multiple of tabWidth). Only
  // meaningful for isSimple() lines.
  qsizetype cellForColumn(QStringView line, qsizetype column) const;
  qreal xForColumn(QStringView line, qsizetype column) const {
    return cellForColumn(line, column) * m_advance;
  }
  // Nearest column boundary to `x`, clamped to the line.
  qsizetype columnForX(QStringView line, qreal x) const;

private:
  void update();

  QFont m_font;
  QFont m_layoutFont;
  int m_tabWidth = 4;
  qreal m_lineHeight = 16;
  qreal m_ascent = 12;
  qreal m_advance = 8;
  bool m_monospace = false;
};

} // namespace qce

#endif // QCE_TEXTMETRICS_H
