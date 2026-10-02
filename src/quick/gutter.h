#ifndef QCE_GUTTER_H
#define QCE_GUTTER_H

#include "core/displaymap.h"
#include "core/textdocument.h"
#include "quick/editorscene.h"
#include "quick/textmetrics.h"
#include "quick/theme.h"

#include <QtCore/QObject>
#include <QtQml/qqmlregistration.h>

class CodeEditor;

namespace qce {

// What a column may look at while measuring and painting. Valid only for the call.
struct GutterContext {
  const TextMetrics *metrics = nullptr;
  const Theme *theme = nullptr;
  const TextDocument *document = nullptr;
  const DisplayMap *map = nullptr;
  qsizetype cursorLine = 0; // line of the primary cursor
  qsizetype lineCount = 1;
  qreal contentY = 0;
  qreal height = 0; // of the editor item
};

// Where a column draws. Coordinates are local to the column; the painter shifts them to the column's
// place in the gutter. Everything lands in the frame plan, so it scrolls with the text.
class GutterPainter {
public:
  GutterPainter(GutterPlan *plan, qreal x, qreal width) : m_plan(plan), m_x(x), m_width(width) {}
  qreal width() const { return m_width; }

  // A rectangle at [x0, x1) of the column, `y` below the row's top; a negative height is the whole row.
  void rect(qsizetype row, qreal x0, qreal x1, const QColor &color, qreal y = 0, qreal height = -1) {
    m_plan->rects.append({row, m_x + x0, m_x + x1, y, height, color});
  }
  void label(qsizetype row, qreal x, std::shared_ptr<LineLayout> layout, const QColor &color) {
    m_plan->labels.append({row, m_x + x, std::move(layout), color});
  }
  void image(qsizetype row, const QRectF &rect, const QImage &image) {
    m_plan->images.append({row, rect.translated(m_x, 0), image});
  }

private:
  GutterPlan *m_plan;
  qreal m_x;
  qreal m_width;
};

// One column of the gutter (GUTTER-01). The editor lays its visible columns out left to right, asks each
// for its width when the layout changes, and has it paint the rows of the frame plan. Built-in
// columns and QML-supplied ones are the same thing to the editor.
class GutterColumn : public QObject {
  Q_OBJECT
  QML_ANONYMOUS
  Q_PROPERTY(bool visible READ isVisible WRITE setVisible NOTIFY visibleChanged FINAL)
  // The requested width in pixels; negative lets the column decide (line numbers follow their digits).
  Q_PROPERTY(qreal width READ width WRITE setWidth NOTIFY widthChanged FINAL)
  // The width the column has in the gutter now.
  Q_PROPERTY(qreal actualWidth READ actualWidth NOTIFY actualWidthChanged FINAL)
  // Whether pressing in the column selects the line (and dragging selects lines).
  Q_PROPERTY(bool selectsLines READ selectsLines WRITE setSelectsLines NOTIFY selectsLinesChanged FINAL)
public:
  explicit GutterColumn(QObject *parent = nullptr) : QObject(parent) {}

  bool isVisible() const { return m_visible; }
  void setVisible(bool visible);
  qreal width() const { return m_width; }
  void setWidth(qreal width);
  qreal actualWidth() const { return m_actualWidth; }
  bool selectsLines() const { return m_selectsLines; }
  void setSelectsLines(bool selects);
  // Left edge in the gutter, as of the last layout.
  qreal x() const { return m_x; }

  // The width to use when none was requested.
  virtual qreal autoWidth(const GutterContext &) const { return 16; }
  qreal measure(const GutterContext &context) const { return m_width >= 0 ? m_width : autoWidth(context); }

  // Called by the editor.
  virtual void attach(CodeEditor *) {}
  virtual void detach(CodeEditor *) {}
  void setPlacement(qreal x, qreal width);
  // Paints `rows` (contiguous, ascending). Not called while the column is hidden.
  virtual void paintRows(const GutterContext &, const QList<FramePlanRow> &, GutterPainter &) {}
  // The view scrolled without the rows changing; columns made of items move them here.
  virtual void scrolled(const GutterContext &) {}

signals:
  void visibleChanged();
  void widthChanged();
  void actualWidthChanged();
  void selectsLinesChanged();
  // Something the column shows changed; the editor repaints it.
  void contentChanged();
  // A press in the column, on the buffer line of the row under the pointer.
  void clicked(qsizetype line, int button, int modifiers);

private:
  bool m_visible = true;
  qreal m_width = -1;
  qreal m_actualWidth = 0;
  qreal m_x = 0;
  bool m_selectsLines = false;
};

// A process-unique id for gutter labels, so a text node knows whether the glyphs it holds are current.
quint64 nextGutterLayoutId();

// Lays out `text` as a single-line label in `font`.
std::shared_ptr<LineLayout> makeLabelLayout(const QString &text, const QFont &font);

} // namespace qce

#endif // QCE_GUTTER_H
