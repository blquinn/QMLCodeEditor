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

FontWrapMeasure::FontWrapMeasure(const QFont &layoutFont, int tabWidth, qreal cellAdvance)
    : m_font(layoutFont), m_tabWidth(tabWidth), m_cell(cellAdvance) {
  const QFontMetricsF fm(m_font);
  for (int cp = 0; cp < 256; ++cp) {
    const bool printable = (cp >= 0x20 && cp < 0x7f) || cp >= 0xa0;
    m_latin[cp] = printable ? fm.horizontalAdvance(QChar(cp)) : m_cell;
  }
}

qreal FontWrapMeasure::advance(char32_t cp) const {
  if (cp < 256)
    return m_latin[cp];
  if (isClusterExtender(cp))
    return 0;
  const QMutexLocker lock(&m_mutex);
  if (const auto it = m_cache.constFind(cp); it != m_cache.constEnd())
    return it.value();
  const qreal width = QFontMetricsF(m_font).horizontalAdvance(QString::fromUcs4(&cp, 1));
  m_cache.insert(cp, width);
  return width;
}

} // namespace qce
