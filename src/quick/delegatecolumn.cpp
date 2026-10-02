#include "quick/delegatecolumn.h"

#include "quick/codeeditor.h"

#include <QtQml/QQmlEngine>
#include <QtQml/qqml.h>

namespace qce {

namespace {
// Item y offsets are relative to an origin row, like the scene's matrices, so they stay precise.
constexpr qsizetype kRebaseDistance = 2048;
constexpr qsizetype kMaxPooled = 64;
} // namespace

DelegateColumn::DelegateColumn(QObject *parent) : GutterColumn(parent) {}
DelegateColumn::~DelegateColumn() {
  delete m_clip; // the items inside go with it; null once the editor has detached
}

void DelegateColumn::attach(CodeEditor *editor) {
  m_clip = new QQuickItem(editor);
  m_clip->setParentItem(editor);
  m_clip->setClip(true);
  m_holder = new QQuickItem(m_clip);
  m_holder->setParentItem(m_clip);
  connect(this, &GutterColumn::visibleChanged, m_clip, [this] { m_clip->setVisible(isVisible()); });
}

void DelegateColumn::detach(CodeEditor *) {
  discardAll();
  delete m_clip; // takes the holder with it
  m_clip = m_holder = nullptr;
}

void DelegateColumn::setDelegate(QQmlComponent *delegate) {
  if (delegate == m_delegate)
    return;
  m_delegate = delegate;
  discardAll();
  emit delegateChanged();
  emit contentChanged();
}

void DelegateColumn::setFirstRowsOnly(bool only) {
  if (only == m_firstRowsOnly)
    return;
  m_firstRowsOnly = only;
  emit firstRowsOnlyChanged();
  emit contentChanged();
}

void DelegateColumn::discardAll() {
  for (QQuickItem *item : std::as_const(m_active))
    delete item;
  m_active.clear();
  qDeleteAll(m_pool);
  m_pool.clear();
}

QQuickItem *DelegateColumn::acquire(const QVariantMap &properties) {
  if (!m_pool.isEmpty()) {
    QQuickItem *item = m_pool.takeLast();
    for (auto it = properties.cbegin(); it != properties.cend(); ++it)
      item->setProperty(qPrintable(it.key()), it.value());
    item->setVisible(true);
    return item;
  }
  if (!m_delegate || !m_delegate->isReady()) {
    if (!m_warned && m_delegate && m_delegate->isError())
      qWarning() << "DelegateColumn:" << m_delegate->errors();
    m_warned = true;
    return nullptr;
  }
  QObject *object = m_delegate->createWithInitialProperties(properties, qmlContext(this));
  auto *item = qobject_cast<QQuickItem *>(object);
  if (!item) {
    delete object;
    if (!m_warned)
      qWarning("DelegateColumn: the delegate must be an Item");
    m_warned = true;
    return nullptr;
  }
  ++m_created;
  item->setParent(m_holder);
  item->setParentItem(m_holder);
  return item;
}

void DelegateColumn::recycle(QQuickItem *item) {
  if (m_pool.size() >= kMaxPooled) {
    delete item;
    return;
  }
  item->setVisible(false);
  m_pool.append(item);
}

// The clip covers the column, the holder carries the scroll offset.
void DelegateColumn::place(const GutterContext &context) {
  m_clip->setVisible(isVisible());
  m_clip->setPosition(QPointF(x(), 0));
  m_clip->setSize(QSizeF(actualWidth(), context.height));
  m_holder->setY(double(m_origin) * context.metrics->lineHeight() - context.contentY);
}

void DelegateColumn::scrolled(const GutterContext &context) {
  if (m_clip)
    place(context);
}

void DelegateColumn::paintRows(const GutterContext &context, const QList<FramePlanRow> &rows, GutterPainter &) {
  if (!m_clip)
    return;
  if (!m_delegate || rows.isEmpty()) {
    for (QQuickItem *item : std::as_const(m_active))
      recycle(item);
    m_active.clear();
    place(context);
    return;
  }
  const qreal lh = context.metrics->lineHeight();
  const bool rebase = !m_haveOrigin || qAbs(rows.first().row - m_origin) > kRebaseDistance;
  if (rebase) {
    m_origin = rows.first().row;
    m_haveOrigin = true;
  }

  // Rows that are no longer shown give their items back before new rows ask for them.
  const qsizetype firstRow = rows.first().row, lastRow = rows.last().row;
  for (auto it = m_active.begin(); it != m_active.end();) {
    const qsizetype row = it.key();
    const bool keep = row >= firstRow && row <= lastRow &&
                      (!m_firstRowsOnly || rows[row - firstRow].display.isFirst());
    if (keep) {
      ++it;
    } else {
      recycle(it.value());
      it = m_active.erase(it);
    }
  }

  for (const FramePlanRow &row : rows) {
    if (m_firstRowsOnly && !row.display.isFirst())
      continue;
    const QVariantMap properties{
      {QStringLiteral("line"), qlonglong(row.display.line)},
      {QStringLiteral("row"), qlonglong(row.row)},
      {QStringLiteral("rowInLine"), qlonglong(row.display.rowInLine)},
      {QStringLiteral("firstRow"), row.display.isFirst()},
      {QStringLiteral("current"), row.display.line == context.cursorLine},
    };
    QQuickItem *item = m_active.value(row.row);
    if (!item) {
      item = acquire(properties);
      if (!item)
        continue;
      m_active.insert(row.row, item);
    } else {
      // Only what can change while the row stays: the cursor moved, lines were inserted above.
      if (item->property("line").toLongLong() != row.display.line)
        item->setProperty("line", qlonglong(row.display.line));
      if (item->property("current").toBool() != (row.display.line == context.cursorLine))
        item->setProperty("current", row.display.line == context.cursorLine);
      if (item->property("rowInLine").toLongLong() != row.display.rowInLine)
        item->setProperty("rowInLine", qlonglong(row.display.rowInLine));
      if (item->property("firstRow").toBool() != row.display.isFirst())
        item->setProperty("firstRow", row.display.isFirst());
    }
    item->setPosition(QPointF(0, double(row.row - m_origin) * lh));
    item->setSize(QSizeF(actualWidth(), lh));
  }
  place(context);
}

} // namespace qce
