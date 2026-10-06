#include "quick/decorationcolumn.h"

#include "quick/codeeditor.h"

#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtQuick/QQuickWindow>

#include <QtCore/QHash>

#include <cmath>

namespace qce {

DecorationColumn::DecorationColumn(QObject *parent) : GutterColumn(parent) {}

DecorationColumn::~DecorationColumn() = default;

qreal DecorationColumn::autoWidth(const GutterContext &context) const { return std::ceil(context.metrics->lineHeight()); }

void DecorationColumn::attach(CodeEditor *editor) {
  m_editor = editor;
  m_connection = connect(editor->decorations(), &DecorationSet::changed, this, [this](qsizetype, qsizetype, quint32 kinds) {
    if (kinds & decorationKindBit(DecorationKind::GutterIcon))
      emit contentChanged();
  });
}

void DecorationColumn::detach(CodeEditor *) {
  disconnect(m_connection);
  m_editor = nullptr;
}

QImage DecorationColumn::glyph(int severity, const QColor &color, int side) {
  const qreal ratio = m_editor && m_editor->window() ? m_editor->window()->effectiveDevicePixelRatio() : 1.0;
  const quint64 key = (quint64(color.rgba()) << 32) | (quint64(side) << 8) | (quint64(severity) << 4) | quint64(ratio * 2);
  if (const auto it = m_glyphs.find(key); it != m_glyphs.end())
    return it->second;
  if (m_glyphs.size() > 32)
    m_glyphs.clear();
  const int pixels = int(std::ceil(side * ratio));
  QImage image(pixels, pixels, QImage::Format_ARGB32_Premultiplied);
  image.setDevicePixelRatio(ratio);
  image.fill(Qt::transparent);
  QPainter p(&image);
  p.setRenderHint(QPainter::Antialiasing);
  const qreal s = side;
  const QRectF box(s * 0.12, s * 0.12, s * 0.76, s * 0.76);
  const QColor ink(0xff, 0xff, 0xff);
  QPen pen(ink, qMax<qreal>(1.2, s / 9), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
  p.setPen(Qt::NoPen);
  p.setBrush(color);
  switch (severity) {
  case ErrorSeverity: // a disc with a cross
    p.drawEllipse(box);
    p.setPen(pen);
    p.drawLine(QPointF(s * 0.36, s * 0.36), QPointF(s * 0.64, s * 0.64));
    p.drawLine(QPointF(s * 0.64, s * 0.36), QPointF(s * 0.36, s * 0.64));
    break;
  case WarningSeverity: { // a triangle with an exclamation mark
    QPainterPath path;
    path.moveTo(s * 0.5, s * 0.1);
    path.lineTo(s * 0.92, s * 0.86);
    path.lineTo(s * 0.08, s * 0.86);
    path.closeSubpath();
    p.drawPath(path);
    p.setPen(pen);
    p.drawLine(QPointF(s * 0.5, s * 0.38), QPointF(s * 0.5, s * 0.6));
    p.drawPoint(QPointF(s * 0.5, s * 0.74));
    break;
  }
  case InfoSeverity: // a disc with an i
    p.drawEllipse(box);
    p.setPen(pen);
    p.drawLine(QPointF(s * 0.5, s * 0.46), QPointF(s * 0.5, s * 0.68));
    p.drawPoint(QPointF(s * 0.5, s * 0.32));
    break;
  default: // a hint is a small dot
    p.drawEllipse(QRectF(s * 0.34, s * 0.34, s * 0.32, s * 0.32));
    break;
  }
  p.end();
  return m_glyphs.emplace(key, image).first->second;
}

void DecorationColumn::paintRows(const GutterContext &context, const QList<FramePlanRow> &rows, GutterPainter &painter) {
  if (!m_editor || rows.isEmpty())
    return;
  const DecorationSet &set = *m_editor->decorations();
  if (set.count(DecorationKind::GutterIcon) == 0)
    return;
  const qsizetype firstLine = rows.first().display.line, lastLine = rows.last().display.line;
  const QList<Decoration> found = set.queryLines(firstLine, lastLine, decorationKindBit(DecorationKind::GutterIcon));
  if (found.isEmpty())
    return;
  // The decoration shown on each line: the highest priority, the earliest added on a tie.
  const Rope &rope = context.document->rope();
  QHash<qsizetype, const Decoration *> byLine;
  for (const Decoration &d : found) {
    const qsizetype line = rope.lineAt(d.start);
    if (line < firstLine || line > lastLine)
      continue;
    const Decoration *&best = byLine[line];
    if (!best || d.priority > best->priority)
      best = &d;
  }
  const qreal lh = context.metrics->lineHeight();
  const int side = qMax(8, int(std::floor(qMin(painter.width(), lh) * 0.85)));
  for (const FramePlanRow &row : rows) {
    if (!row.display.isFirst())
      continue;
    const Decoration *d = byLine.value(row.display.line, nullptr);
    if (!d)
      continue;
    QImage image = d->icon;
    QSizeF size(side, side);
    if (image.isNull()) {
      const QColor color = d->color.isValid() ? d->color : context.theme->severityColor(d->severity);
      image = glyph(d->severity, color, side);
    } else {
      size = QSizeF(image.size()).scaled(QSizeF(side, side), Qt::KeepAspectRatio);
    }
    painter.image(row.row, QRectF((painter.width() - size.width()) / 2, (lh - size.height()) / 2, size.width(), size.height()), image);
  }
}

} // namespace qce
