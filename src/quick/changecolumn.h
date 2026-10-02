#ifndef QCE_CHANGECOLUMN_H
#define QCE_CHANGECOLUMN_H

#include "core/linemarkerset.h"
#include "quick/gutter.h"

#include <memory>

namespace qce {

// Marks the lines edited since the text was loaded or saved (GUTTER-07): a bar for lines that were
// changed or added, a notch where whole lines were removed. Marks are anchored ranges, so they
// follow later edits. There is no diff against the saved text, so undoing back to it leaves the
// marks until the next save; hosts that have a real diff (git) use a MarkerColumn instead.
class ChangeColumn : public GutterColumn {
  Q_OBJECT
  QML_ELEMENT
public:
  enum Kind { Modified = 1, Deleted = 2 };

  explicit ChangeColumn(QObject *parent = nullptr);
  ~ChangeColumn() override;

  // Forgets every mark, as saving does.
  Q_INVOKABLE void reset();
  // Lines currently marked as modified, for tests and hosts: true if `line` carries a mark of any kind.
  Q_INVOKABLE bool isChanged(qsizetype line) const;

  qreal autoWidth(const GutterContext &) const override { return 6; }
  void attach(CodeEditor *editor) override;
  void detach(CodeEditor *editor) override;
  void paintRows(const GutterContext &context, const QList<FramePlanRow> &rows, GutterPainter &painter) override;

private:
  void onChanged(const TextChange &change);

  TextDocument *m_document = nullptr;
  std::unique_ptr<LineMarkerSet> m_set;
};

} // namespace qce

#endif // QCE_CHANGECOLUMN_H
