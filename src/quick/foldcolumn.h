#ifndef QCE_FOLDCOLUMN_H
#define QCE_FOLDCOLUMN_H

#include "quick/gutter.h"

#include <QtGui/QImage>

#include <map>
#include <tuple>

namespace qce {

// Chevrons on the lines that start a foldable region (FOLD-04): pointing down while open, right while
// folded. A press toggles the fold. While the pointer is over a header, the column shades the rows of
// its region and draws the chevron in the hover color.
class FoldColumn : public GutterColumn {
  Q_OBJECT
  QML_ELEMENT
  // Open regions show their chevron only while the pointer is in the column; folded ones always do.
  Q_PROPERTY(bool showOnHover READ showOnHover WRITE setShowOnHover NOTIFY showOnHoverChanged FINAL)
public:
  explicit FoldColumn(QObject *parent = nullptr);

  bool showOnHover() const { return m_showOnHover; }
  void setShowOnHover(bool show);

  qreal autoWidth(const GutterContext &context) const override;
  void attach(CodeEditor *editor) override;
  void detach(CodeEditor *editor) override;
  void paintRows(const GutterContext &context, const QList<FramePlanRow> &rows, GutterPainter &painter) override;
  void hoverLine(qsizetype line) override;

signals:
  void showOnHoverChanged();

private:
  QImage chevron(bool folded, const QColor &color, const QSizeF &size);

  CodeEditor *m_editor = nullptr;
  bool m_showOnHover = false;
  qsizetype m_hoverLine = -1;
  // Rendered chevrons by (folded, color, width, height, ratio); few exist at a time.
  std::map<std::tuple<bool, QRgb, int, int, int>, QImage> m_images;
};

} // namespace qce

#endif // QCE_FOLDCOLUMN_H
