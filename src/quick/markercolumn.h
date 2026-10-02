#ifndef QCE_MARKERCOLUMN_H
#define QCE_MARKERCOLUMN_H

#include "core/linemarkerset.h"
#include "quick/gutter.h"

#include <QtCore/QHash>
#include <QtCore/QUrl>
#include <QtCore/QVariantMap>
#include <QtGui/QColor>
#include <QtGui/QImage>

#include <memory>

namespace qce {

// A column that hosts fill with per-line marks (GUTTER-04): git change bars, breakpoints, diagnostics.
// Marks are line ranges held by anchors, so they follow edits; a mark added to the line of a
// breakpoint stays on that line as text is typed above it. A text reset (load, setText) removes them.
class MarkerColumn : public GutterColumn {
  Q_OBJECT
  QML_ELEMENT
public:
  enum Style {
    Bar, // a strip along the left edge, over every row of the line(s)
    Icon // an image, on the first row of the line only
  };
  Q_ENUM(Style)

  explicit MarkerColumn(QObject *parent = nullptr);
  ~MarkerColumn() override;

  // Adds a mark and returns its id. `options` (all optional): lastLine (a mark over several lines),
  // color, icon (a URL of an image: file:, qrc: or :/), style (Bar or Icon; Icon when an icon is
  // given), priority (the highest priority wins where marks overlap), barWidth.
  Q_INVOKABLE int addMarker(qsizetype line, const QVariantMap &options = {});
  Q_INVOKABLE bool removeMarker(int id);
  Q_INVOKABLE void clearMarkers();
  // Removes every mark that touches `line`; true if there was one.
  Q_INVOKABLE bool removeMarkersAt(qsizetype line);
  // The ids of the marks touching `line`, highest priority first.
  Q_INVOKABLE QList<int> markersAt(qsizetype line) const;
  Q_INVOKABLE qsizetype markerCount() const;

  qreal autoWidth(const GutterContext &) const override { return 16; }
  void attach(CodeEditor *editor) override;
  void detach(CodeEditor *editor) override;
  void paintRows(const GutterContext &context, const QList<FramePlanRow> &rows, GutterPainter &painter) override;

private:
  struct Look {
    QColor color;
    QImage icon;
    Style style = Bar;
    qreal barWidth = 3;
  };
  static QImage loadIcon(const QUrl &url);

  std::unique_ptr<LineMarkerSet> m_set;
  QHash<int, Look> m_looks;
  QHash<QString, QImage> m_icons;
  QColor m_defaultColor{0xe5, 0x14, 0x00};
};

} // namespace qce

#endif // QCE_MARKERCOLUMN_H
