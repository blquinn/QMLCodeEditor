#include "quick/foldcolumn.h"

#include "quick/codeeditor.h"

#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtQuick/QQuickWindow>

#include <cmath>

namespace qce {

FoldColumn::FoldColumn(QObject *parent) : GutterColumn(parent) {}

void FoldColumn::setShowOnHover(bool show) {
  if (show == m_showOnHover)
    return;
  m_showOnHover = show;
  emit showOnHoverChanged();
  emit contentChanged();
}

qreal FoldColumn::autoWidth(const GutterContext &context) const { return std::ceil(context.metrics->lineHeight()); }

void FoldColumn::attach(CodeEditor *editor) {
  m_editor = editor;
  connect(this, &GutterColumn::clicked, this, [this](qsizetype line, int button, int) {
    if (button == Qt::LeftButton && m_editor)
      m_editor->toggleFold(line);
  });
}

void FoldColumn::detach(CodeEditor *) {
  disconnect(this, &GutterColumn::clicked, this, nullptr);
  m_editor = nullptr;
  m_hoverLine = -1;
}

void FoldColumn::hoverLine(qsizetype line) {
  if (line == m_hoverLine)
    return;
  const bool enteredOrLeft = (line < 0) != (m_hoverLine < 0);
  m_hoverLine = line;
  // The chevrons only depend on the hover when open ones are hidden; the band always does.
  Q_UNUSED(enteredOrLeft);
  emit contentChanged();
}

// A chevron centred in a cell of `size`, drawn once per state and kept.
QImage FoldColumn::chevron(bool folded, const QColor &color, const QSizeF &size) {
  const qreal ratio = m_editor && m_editor->window() ? m_editor->window()->effectiveDevicePixelRatio() : 1.0;
  const auto key = std::make_tuple(folded, color.rgba(), int(size.width()), int(size.height()), int(ratio * 100));
  if (const auto it = m_images.find(key); it != m_images.end())
    return it->second;
  if (m_images.size() > 16)
    m_images.clear();
  QImage image(QSize(int(std::ceil(size.width() * ratio)), int(std::ceil(size.height() * ratio))), QImage::Format_ARGB32_Premultiplied);
  image.setDevicePixelRatio(ratio);
  image.fill(Qt::transparent);
  QPainter p(&image);
  p.setRenderHint(QPainter::Antialiasing);
  QPen pen(color, qMax<qreal>(1.2, size.height() / 14), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
  p.setPen(pen);
  const qreal half = size.height() * 0.17; // half the chevron's extent along its pointing axis
  const QPointF c(size.width() / 2, size.height() / 2);
  QPainterPath path;
  if (folded) { // >
    path.moveTo(c.x() - half * 0.6, c.y() - half * 1.4);
    path.lineTo(c.x() + half * 0.9, c.y());
    path.lineTo(c.x() - half * 0.6, c.y() + half * 1.4);
  } else { // v
    path.moveTo(c.x() - half * 1.4, c.y() - half * 0.6);
    path.lineTo(c.x(), c.y() + half * 0.9);
    path.lineTo(c.x() + half * 1.4, c.y() - half * 0.6);
  }
  p.drawPath(path);
  p.end();
  return m_images.emplace(key, image).first->second;
}

void FoldColumn::paintRows(const GutterContext &context, const QList<FramePlanRow> &rows, GutterPainter &painter) {
  if (!m_editor || rows.isEmpty())
    return;
  const QList<FoldRange> ranges = m_editor->foldRangesIn(rows.first().display.line, rows.last().display.line);
  if (ranges.isEmpty())
    return;
  const FoldMap &folds = context.map->folds();
  const qreal lh = context.metrics->lineHeight();
  const QSizeF cell(painter.width(), lh);
  const bool pointerInside = m_hoverLine >= 0;
  qsizetype next = 0;
  for (const FramePlanRow &row : rows) {
    const qsizetype line = row.display.line;
    // The range hovered: shade the rows of the lines it covers (a folded one has only its header).
    for (const FoldRange &range : ranges) {
      if (range.startLine == m_hoverLine && line >= range.startLine && line <= range.endLine)
        painter.rect(row.row, 0, painter.width(), context.theme->foldRangeHover());
    }
    if (!row.display.isFirst())
      continue;
    while (next < ranges.size() && ranges[next].startLine < line)
      ++next;
    if (next >= ranges.size() || ranges[next].startLine != line)
      continue;
    const bool folded = folds.isFolded(line);
    if (!folded && m_showOnHover && !pointerInside)
      continue;
    const QColor color = line == m_hoverLine ? context.theme->foldMarkerHover() : context.theme->foldMarker();
    painter.image(row.row, QRectF(QPointF(0, 0), cell), chevron(folded, color, cell));
  }
}

} // namespace qce
