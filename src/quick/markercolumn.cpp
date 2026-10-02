#include "quick/markercolumn.h"

#include "quick/codeeditor.h"

#include <algorithm>

namespace qce {

MarkerColumn::MarkerColumn(QObject *parent) : GutterColumn(parent) {}
MarkerColumn::~MarkerColumn() = default;

void MarkerColumn::attach(CodeEditor *editor) {
  m_set = std::make_unique<LineMarkerSet>(editor->document());
  connect(m_set.get(), &LineMarkerSet::changed, this, &GutterColumn::contentChanged);
  connect(editor->document(), &TextDocument::textReset, this, &MarkerColumn::clearMarkers);
}

void MarkerColumn::detach(CodeEditor *editor) {
  disconnect(editor->document(), &TextDocument::textReset, this, &MarkerColumn::clearMarkers);
  m_set.reset();
  m_looks.clear();
}

QImage MarkerColumn::loadIcon(const QUrl &url) {
  QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
  if (url.scheme() == QLatin1String("qrc"))
    path = QLatin1Char(':') + url.path();
  return QImage(path);
}

int MarkerColumn::addMarker(qsizetype line, const QVariantMap &options) {
  if (!m_set)
    return 0;
  Look look;
  look.color = options.contains(QStringLiteral("color")) ? options.value(QStringLiteral("color")).value<QColor>()
                                                         : m_defaultColor;
  if (options.contains(QStringLiteral("icon"))) {
    const QUrl url = options.value(QStringLiteral("icon")).toUrl();
    QImage &icon = m_icons[url.toString()];
    if (icon.isNull())
      icon = loadIcon(url);
    look.icon = icon;
    look.style = Icon;
  }
  if (options.contains(QStringLiteral("style")))
    look.style = Style(options.value(QStringLiteral("style")).toInt());
  if (look.style == Icon && look.icon.isNull())
    look.style = Bar;
  look.barWidth = options.value(QStringLiteral("barWidth"), 3).toReal();
  const qsizetype last = options.value(QStringLiteral("lastLine"), line).toLongLong();
  const int id = m_set->add(line, qMax(line, last), look.style, options.value(QStringLiteral("priority")).toInt());
  m_looks.insert(id, look);
  return id;
}

bool MarkerColumn::removeMarker(int id) {
  if (!m_set || !m_set->remove(id))
    return false;
  m_looks.remove(id);
  return true;
}

void MarkerColumn::clearMarkers() {
  if (!m_set)
    return;
  m_set->clear();
  m_looks.clear();
}

QList<int> MarkerColumn::markersAt(qsizetype line) const {
  QList<int> ids;
  if (!m_set)
    return ids;
  QList<LineMarkerSet::Marker> found = m_set->query(line, line);
  std::stable_sort(found.begin(), found.end(), [](const auto &a, const auto &b) { return a.priority > b.priority; });
  for (const auto &marker : std::as_const(found))
    ids.append(marker.id);
  return ids;
}

bool MarkerColumn::removeMarkersAt(qsizetype line) {
  const QList<int> ids = markersAt(line);
  for (int id : ids)
    removeMarker(id);
  return !ids.isEmpty();
}

qsizetype MarkerColumn::markerCount() const { return m_set ? m_set->size() : 0; }

void MarkerColumn::paintRows(const GutterContext &context, const QList<FramePlanRow> &rows, GutterPainter &painter) {
  if (!m_set || rows.isEmpty() || m_set->size() == 0)
    return;
  QList<LineMarkerSet::Marker> found = m_set->query(rows.first().display.line, rows.last().display.line);
  if (found.isEmpty())
    return;
  // Lowest priority first, so higher ones paint over them.
  std::stable_sort(found.begin(), found.end(), [](const auto &a, const auto &b) { return a.priority < b.priority; });
  const qreal width = painter.width();
  for (const FramePlanRow &row : rows) {
    const qsizetype line = row.display.line;
    for (const auto &marker : std::as_const(found)) {
      if (line < marker.firstLine || line > marker.lastLine)
        continue;
      const Look look = m_looks.value(marker.id);
      if (look.style == Bar) {
        painter.rect(row.row, 0, qMin(look.barWidth, width), look.color);
      } else if (row.display.isFirst()) {
        // Centred in the column and no larger than the row.
        const qreal lh = context.metrics->lineHeight();
        const qreal side = qMin(width, lh);
        const QSize size = look.icon.size().scaled(QSize(int(side), int(side)), Qt::KeepAspectRatio);
        painter.image(row.row, QRectF((width - size.width()) / 2, (lh - size.height()) / 2, size.width(), size.height()), look.icon);
      }
    }
  }
}

} // namespace qce
