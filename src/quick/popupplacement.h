#ifndef QCE_POPUPPLACEMENT_H
#define QCE_POPUPPLACEMENT_H

#include <QtCore/QPointF>
#include <QtCore/QRectF>
#include <QtCore/QSizeF>

namespace qce {

// Where a popup of `size` goes so it hangs off `anchor` (the character or row it is about) and stays
// inside `bounds` (DIAG-04). All three are in one coordinate system; the result is the popup's
// top-left corner.
//
// The popup sits below the anchor with its left edge on the anchor's. When it does not fit below it
// flips above; when it fits neither way it goes on the side with more room, pushed back inside the
// bounds (it may then cover the anchor). Horizontally it is pushed left to fit and, if it is wider
// than the bounds, starts at their left edge. `gap` is the space left between popup and anchor.
QPointF placePopup(const QRectF &anchor, const QSizeF &size, const QRectF &bounds, qreal gap = 2);

} // namespace qce

#endif // QCE_POPUPPLACEMENT_H
