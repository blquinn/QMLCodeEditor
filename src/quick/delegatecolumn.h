#ifndef QCE_DELEGATECOLUMN_H
#define QCE_DELEGATECOLUMN_H

#include "quick/gutter.h"

#include <QtCore/QHash>
#include <QtQml/QQmlComponent>
#include <QtQuick/QQuickItem>

namespace qce {

// A column drawn by the host in QML (GUTTER-06): `delegate` is instantiated once for each row in the
// frame plan, i.e. the visible rows plus a margin, never for the rest of the document. Delegates are
// pooled: a row that scrolls out hands its item to a row that scrolls in, with the properties
// updated, so scrolling creates nothing once the pool is warm.
//
// Each delegate gets these properties (declare them `required property`): `line` (buffer line),
// `row` (display row), `rowInLine` (0 for the first), `firstRow`, `current` (the cursor's line). It
// is sized to the column's width and the editor's line height; position is managed here.
//
// Items move with the scroll position by one holder item's y, not one by one.
class DelegateColumn : public GutterColumn {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(QQmlComponent *delegate READ delegate WRITE setDelegate NOTIFY delegateChanged FINAL)
  // Wrapped lines get a delegate on their first row only (the default), or on every row.
  Q_PROPERTY(bool firstRowsOnly READ firstRowsOnly WRITE setFirstRowsOnly NOTIFY firstRowsOnlyChanged FINAL)
public:
  explicit DelegateColumn(QObject *parent = nullptr);
  ~DelegateColumn() override;

  QQmlComponent *delegate() const { return m_delegate; }
  void setDelegate(QQmlComponent *delegate);
  bool firstRowsOnly() const { return m_firstRowsOnly; }
  void setFirstRowsOnly(bool only);

  // Delegates currently showing a row, and how many were ever created; for tests and tuning.
  Q_INVOKABLE int delegateCount() const { return int(m_active.size()); }
  Q_INVOKABLE int createdCount() const { return m_created; }
  // The delegate showing display row `row`, or null.
  Q_INVOKABLE QQuickItem *delegateForRow(qsizetype row) const { return m_active.value(row); }

  qreal autoWidth(const GutterContext &) const override { return 20; }
  void attach(CodeEditor *editor) override;
  void detach(CodeEditor *editor) override;
  void paintRows(const GutterContext &context, const QList<FramePlanRow> &rows, GutterPainter &painter) override;
  void scrolled(const GutterContext &context) override;

signals:
  void delegateChanged();
  void firstRowsOnlyChanged();

private:
  QQuickItem *acquire(const QVariantMap &properties);
  void recycle(QQuickItem *item);
  void discardAll();
  void place(const GutterContext &context);

  QQmlComponent *m_delegate = nullptr;
  bool m_firstRowsOnly = true;
  QQuickItem *m_clip = nullptr;   // the column's rectangle, clipping
  QQuickItem *m_holder = nullptr; // moves with the scroll position
  QHash<qsizetype, QQuickItem *> m_active;
  QList<QQuickItem *> m_pool;
  qsizetype m_origin = 0;
  bool m_haveOrigin = false;
  int m_created = 0;
  bool m_warned = false;
};

} // namespace qce

#endif // QCE_DELEGATECOLUMN_H
