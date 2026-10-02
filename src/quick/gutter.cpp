#include "quick/gutter.h"

#include <atomic>

namespace qce {

void GutterColumn::setVisible(bool visible) {
  if (visible == m_visible)
    return;
  m_visible = visible;
  emit visibleChanged();
  emit contentChanged();
}

void GutterColumn::setWidth(qreal width) {
  if (width == m_width)
    return;
  m_width = width;
  emit widthChanged();
  emit contentChanged();
}

void GutterColumn::setSelectsLines(bool selects) {
  if (selects == m_selectsLines)
    return;
  m_selectsLines = selects;
  emit selectsLinesChanged();
}

void GutterColumn::setPlacement(qreal x, qreal width) {
  m_x = x;
  if (width != m_actualWidth) {
    m_actualWidth = width;
    emit actualWidthChanged();
  }
}

quint64 nextGutterLayoutId() {
  static std::atomic<quint64> next{1};
  return next.fetch_add(1, std::memory_order_relaxed);
}

std::shared_ptr<LineLayout> makeLabelLayout(const QString &text, const QFont &font) {
  auto result = std::make_shared<LineLayout>();
  auto layout = std::make_unique<QTextLayout>(text, font);
  QTextOption option;
  option.setWrapMode(QTextOption::NoWrap);
  layout->setTextOption(option);
  layout->setCacheEnabled(true);
  layout->beginLayout();
  QTextLine line = layout->createLine();
  line.setLineWidth(1e9);
  line.setPosition(QPointF(0, 0));
  result->width = line.naturalTextWidth();
  layout->endLayout();
  result->id = nextGutterLayoutId();
  result->layout = std::move(layout);
  result->text = text;
  return result;
}

} // namespace qce
