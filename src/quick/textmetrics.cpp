#include "textmetrics.h"

#include <QtCore/QList>
#include <QtGui/QFontDatabase>
#include <QtGui/QFontMetricsF>

#include <cmath>

namespace qce {

QFont TextMetrics::defaultMonospaceFont() {
  QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  if (TextMetrics(font).isMonospace())
    return font;
  for (const QString &family : QFontDatabase::families()) {
    if (!QFontDatabase::isFixedPitch(family))
      continue;
    QFont candidate(family, font.pointSize() > 0 ? font.pointSize() : 10);
    if (TextMetrics(candidate).isMonospace())
      return candidate;
  }
  return font;
}

void TextMetrics::setFont(const QFont &font) {
  m_font = font;
  update();
}

void TextMetrics::setTabWidth(int columns) { m_tabWidth = qMax(1, columns); }

void TextMetrics::update() {
  m_layoutFont = m_font;
  m_layoutFont.setKerning(false);
  m_layoutFont.setFeature("liga", 0);
  m_layoutFont.setFeature("clig", 0);
  m_layoutFont.setFeature("calt", 0);

  const QFontMetricsF fm(m_layoutFont);
  m_advance = fm.horizontalAdvance(QLatin1Char('M'));
  m_monospace = qAbs(fm.horizontalAdvance(QLatin1Char('i')) - m_advance) < 1e-3 &&
                qAbs(fm.horizontalAdvance(QLatin1Char('W')) - m_advance) < 1e-3 && m_advance > 0;
  m_ascent = std::ceil(fm.ascent());
  m_lineHeight = std::ceil(fm.ascent()) + std::ceil(fm.descent()) + std::ceil(fm.leading());
}

bool TextMetrics::isSimple(QStringView line) const {
  if (!m_monospace)
    return false;
  for (const QChar c : line) {
    const char16_t u = c.unicode();
    if ((u < 0x20 || u > 0x7e) && u != u'\t')
      return false;
  }
  return true;
}

qsizetype TextMetrics::cellForColumn(QStringView line, qsizetype column) const {
  column = qBound<qsizetype>(0, column, line.size());
  qsizetype cell = 0;
  for (qsizetype i = 0; i < column; ++i)
    cell += line[i] == u'\t' ? m_tabWidth - cell % m_tabWidth : 1;
  return cell;
}

qsizetype TextMetrics::columnForX(QStringView line, qreal x) const {
  qsizetype cell = 0;
  for (qsizetype i = 0; i < line.size(); ++i) {
    const qsizetype next = cell + (line[i] == u'\t' ? m_tabWidth - cell % m_tabWidth : 1);
    // Boundary i is nearer than boundary i+1 when x is left of the middle of this cell.
    if (x < (cell + next) * 0.5 * m_advance)
      return i;
    cell = next;
  }
  return line.size();
}

} // namespace qce
