#ifndef QCE_DECORATIONCOLUMN_H
#define QCE_DECORATIONCOLUMN_H

#include "quick/gutter.h"

#include <QtCore/QMetaObject>
#include <QtGui/QImage>

#include <tuple>
#include <unordered_map>

class CodeEditor;

namespace qce {

// Shows the gutter icons of the editor's decorations (DIAG-01): an image on the first row of the line
// each GutterIcon decoration starts on. A decoration without an icon of its own gets a glyph for its
// severity (error, warning, information, hint). Where several decorations share a line the one with
// the highest priority is shown. The icons are the editor's decorations, so they follow edits.
class DecorationColumn : public GutterColumn {
  Q_OBJECT
  QML_ELEMENT
public:
  explicit DecorationColumn(QObject *parent = nullptr);
  ~DecorationColumn() override;

  qreal autoWidth(const GutterContext &context) const override;
  void attach(CodeEditor *editor) override;
  void detach(CodeEditor *editor) override;
  void paintRows(const GutterContext &context, const QList<FramePlanRow> &rows, GutterPainter &painter) override;

private:
  // The glyph for `severity` in a square of `side` logical pixels, cached.
  QImage glyph(int severity, const QColor &color, int side);

  CodeEditor *m_editor = nullptr;
  QMetaObject::Connection m_connection;
  std::unordered_map<quint64, QImage> m_glyphs;
};

} // namespace qce

#endif // QCE_DECORATIONCOLUMN_H
