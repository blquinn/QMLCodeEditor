#include "quick/editorscene.h"

#include <QtGui/QMatrix4x4>
#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGGeometry>
#include <QtQuick/QSGOpacityNode>
#include <QtQuick/QSGRectangleNode>
#include <QtQuick/QSGTextNode>

#include <cmath>

namespace qce {

namespace {
// Rows further than this from the origin are re-based; at 20 px per row the largest offset stays
// near 40k px, well inside float precision.
constexpr qsizetype kRebaseDistance = 2048;
// Spare node pairs kept for rows scrolling in; more than a fast flick needs.
constexpr qsizetype kMaxFree = 128;
} // namespace

EditorScene::EditorScene(QQuickWindow *window) : m_window(window) {
  m_background = window->createRectangleNode();
  appendChildNode(m_background);

  m_clip = new QSGClipNode;
  m_clip->setIsRectangular(true);
  auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 4);
  geometry->setDrawingMode(QSGGeometry::DrawTriangleStrip);
  m_clip->setGeometry(geometry);
  m_clip->setFlag(QSGNode::OwnsGeometry);
  appendChildNode(m_clip);

  m_scroll = new QSGTransformNode;
  m_clip->appendChildNode(m_scroll);

  m_backdrop = new QSGNode;
  m_rows = new QSGNode;
  m_cursorFade = new QSGOpacityNode;
  m_scroll->appendChildNode(m_backdrop);
  m_scroll->appendChildNode(m_rows);
  m_scroll->appendChildNode(m_cursorFade);
  // Fixed order gives the stacking: current line, selection, marks, then (above the text) the cursor.
  m_currentLineBatch = std::make_unique<RectBatch>(window);
  m_selectionBatch = std::make_unique<RectBatch>(window);
  m_markBatch = std::make_unique<RectBatch>(window);
  m_cursorBatch = std::make_unique<RectBatch>(window);
  m_backdrop->appendChildNode(m_currentLineBatch->node());
  m_backdrop->appendChildNode(m_selectionBatch->node());
  m_backdrop->appendChildNode(m_markBatch->node());
  m_cursorFade->appendChildNode(m_cursorBatch->node());
}

bool EditorScene::RectBatch::update(const QList<QRectF> &rects, const QColor &color) {
  if (rects == m_rects && color == m_color)
    return false;
  while (m_nodes.size() > rects.size())
    delete m_nodes.takeLast();
  while (m_nodes.size() < rects.size()) {
    m_nodes.append(m_window->createRectangleNode());
    m_group->appendChildNode(m_nodes.last());
  }
  for (qsizetype i = 0; i < rects.size(); ++i) {
    if (i >= m_rects.size() || rects[i] != m_rects[i])
      m_nodes[i]->setRect(rects[i]);
    if (color != m_color || i >= m_rects.size())
      m_nodes[i]->setColor(color);
  }
  m_rects = rects;
  m_color = color;
  return true;
}

void EditorScene::syncOverlays(const FrameParams &p) {
  const qreal lh = p.lineHeight;
  auto rectFor = [&](const RowSpan &span) {
    return QRectF(
      span.x0, double(span.row - m_originRow) * lh + span.y, span.x1 - span.x0,
      span.height < 0 ? lh : span.height
    );
  };
  auto rects = [&](const QList<RowSpan> *spans) {
    QList<QRectF> out;
    if (spans)
      for (const RowSpan &span : *spans)
        out.append(rectFor(span));
    return out;
  };
  bool changed = m_currentLineBatch->update(rects(p.currentLine), p.currentLineColor);
  changed |= m_selectionBatch->update(rects(p.selection), p.selectionColor);
  changed |= m_markBatch->update(rects(p.marks), p.markColor);
  changed |= m_cursorBatch->update(rects(p.cursors), p.cursorColor);
  if (changed)
    ++m_stats.overlayUpdates;
  m_cursorFade->setOpacity(p.cursorVisible ? 1.0 : 0.0);
}

EditorScene::~EditorScene() {
  // Attached items belong to the tree and die with it; pooled ones are ours to delete.
  for (auto &[row, item] : m_active)
    delete item;
  for (Item *item : std::as_const(m_free)) {
    delete item->transform; // takes the text node with it
    delete item;
  }
}

EditorScene::Item *EditorScene::acquire() {
  Item *item;
  if (!m_free.isEmpty()) {
    item = m_free.takeLast();
    ++m_stats.nodesRecycled;
  } else {
    item = new Item;
    item->transform = new QSGTransformNode;
    item->text = m_window->createTextNode();
    item->transform->appendChildNode(item->text);
    ++m_stats.nodesCreated;
  }
  if (!item->attached) {
    m_rows->appendChildNode(item->transform);
    item->attached = true;
  }
  return item;
}

void EditorScene::release(Item *item) {
  m_rows->removeChildNode(item->transform);
  item->attached = false;
  item->row = -1;
  item->layoutId = 0;
  if (m_free.size() >= kMaxFree) {
    delete item->transform;
    delete item;
    return;
  }
  m_free.append(item);
}

void EditorScene::sync(const FrameParams &p) {
  m_background->setRect(p.viewport);
  m_background->setColor(p.background);

  QSGGeometry::Point2D *v = m_clip->geometry()->vertexDataAsPoint2D();
  v[0].set(float(p.viewport.left()), float(p.viewport.top()));
  v[1].set(float(p.viewport.right()), float(p.viewport.top()));
  v[2].set(float(p.viewport.left()), float(p.viewport.bottom()));
  v[3].set(float(p.viewport.right()), float(p.viewport.bottom()));
  m_clip->geometry()->markVertexDataDirty();
  m_clip->setClipRect(p.viewport);
  m_clip->markDirty(QSGNode::DirtyGeometry);

  const QList<FramePlanRow> &rows = *p.rows;
  if (rows.isEmpty()) {
    for (auto &[row, item] : m_active)
      release(item);
    m_active.clear();
    syncOverlays(p);
    return;
  }
  const qsizetype firstRow = rows.first().row;
  const qsizetype lastRow = rows.last().row;

  if (!m_haveOrigin || qAbs(firstRow - m_originRow) > kRebaseDistance) {
    m_originRow = firstRow;
    m_haveOrigin = true;
  }

  // The scroll transform is the only thing that moves while scrolling.
  QMatrix4x4 scroll;
  scroll.translate(float(-p.contentX), float(double(m_originRow) * p.lineHeight - p.contentY));
  m_scroll->setMatrix(scroll);

  // Rows that left the plan hand their nodes back to the pool before new rows ask for them.
  for (auto it = m_active.begin(); it != m_active.end();) {
    if (it->first < firstRow || it->first > lastRow) {
      release(it->second);
      it = m_active.erase(it);
    } else {
      ++it;
    }
  }

  const bool colorChanged = m_textColor != p.foreground;
  m_textColor = p.foreground;

  for (const FramePlanRow &planRow : rows) {
    Item *item;
    const auto found = m_active.find(planRow.row);
    if (found != m_active.end()) {
      item = found->second;
    } else {
      item = acquire();
      item->row = planRow.row;
      m_active.emplace(planRow.row, item);
    }

    if (item->originRow != m_originRow || item->lineHeight != p.lineHeight || item->layoutId == 0) {
      QMatrix4x4 m;
      m.translate(0, float(double(planRow.row - m_originRow) * p.lineHeight));
      item->transform->setMatrix(m);
      item->originRow = m_originRow;
      item->lineHeight = p.lineHeight;
      ++m_stats.matrixUpdates;
    }

    if (item->layoutId != planRow.layout->id || colorChanged) {
      item->text->clear();
      item->text->setColor(p.foreground);
      item->text->addTextLayout(QPointF(0, 0), planRow.layout->layout.get());
      item->layoutId = planRow.layout->id;
      ++m_stats.linesFilled;
    }
  }

  syncOverlays(p);
}

} // namespace qce
