#include "quick/popupplacement.h"

#include <QtCore/QtGlobal>

namespace qce {

QPointF placePopup(const QRectF &anchor, const QSizeF &size, const QRectF &bounds, qreal gap) {
  const qreal below = anchor.bottom() + gap;
  const qreal above = anchor.top() - gap - size.height();
  qreal y;
  if (below + size.height() <= bounds.bottom()) {
    y = below;
  } else if (above >= bounds.top()) {
    y = above;
  } else {
    // Neither side has room: take the one with more, as far inside the bounds as it will go.
    const qreal roomBelow = bounds.bottom() - below;
    const qreal roomAbove = anchor.top() - gap - bounds.top();
    y = roomBelow >= roomAbove ? bounds.bottom() - size.height() : bounds.top();
    y = qMax(y, bounds.top());
  }
  qreal x = anchor.left();
  if (x + size.width() > bounds.right())
    x = bounds.right() - size.width();
  x = qMax(x, bounds.left());
  return {x, y};
}

} // namespace qce
