#include "quick/editorscene.h"

#include <QtGui/QMatrix4x4>
#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGGeometry>
#include <QtQuick/QSGImageNode>
#include <QtQuick/QSGOpacityNode>
#include <QtQuick/QSGRectangleNode>
#include <QtQuick/QSGTextNode>
#include <QtQuick/QSGTexture>

#include <cmath>

namespace qce {

namespace {
// Rows further than this from the origin are re-based; at 20 px per row the largest offset stays
// near 40k px, well inside float precision.
constexpr qsizetype kRebaseDistance = 2048;
// Spare node pairs kept for rows scrolling in; more than a fast flick needs.
constexpr qsizetype kMaxFree = 128;
} // namespace

namespace {

QSGClipNode *createClipNode() {
  auto *clip = new QSGClipNode;
  clip->setIsRectangular(true);
  auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 4);
  geometry->setDrawingMode(QSGGeometry::DrawTriangleStrip);
  clip->setGeometry(geometry);
  clip->setFlag(QSGNode::OwnsGeometry);
  return clip;
}

void setClipRect(QSGClipNode *clip, const QRectF &rect) {
  QSGGeometry::Point2D *v = clip->geometry()->vertexDataAsPoint2D();
  v[0].set(float(rect.left()), float(rect.top()));
  v[1].set(float(rect.right()), float(rect.top()));
  v[2].set(float(rect.left()), float(rect.bottom()));
  v[3].set(float(rect.right()), float(rect.bottom()));
  clip->geometry()->markVertexDataDirty();
  clip->setClipRect(rect);
  clip->markDirty(QSGNode::DirtyGeometry);
}

} // namespace

EditorScene::EditorScene(QQuickWindow *window) : m_window(window) {
  m_background = window->createRectangleNode();
  appendChildNode(m_background);

  m_clip = createClipNode();
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
  m_chipBatch = std::make_unique<RectBatch>(window);
  m_chipDotBatch = std::make_unique<RectBatch>(window);
  m_cursorBatch = std::make_unique<RectBatch>(window);
  m_backdrop->appendChildNode(m_currentLineBatch->node());
  m_backdrop->appendChildNode(m_selectionBatch->node());
  m_backdrop->appendChildNode(m_markBatch->node());
  m_backdrop->appendChildNode(m_chipBatch->node());
  m_backdrop->appendChildNode(m_chipDotBatch->node());
  m_cursorFade->appendChildNode(m_cursorBatch->node());
  m_textPool.parent = m_rows;

  // The gutter comes after the text so it paints over anything the text clip lets through.
  m_gutterBackground = window->createRectangleNode();
  appendChildNode(m_gutterBackground);
  m_gutterClip = createClipNode();
  appendChildNode(m_gutterClip);
  m_gutterScroll = new QSGTransformNode;
  m_gutterClip->appendChildNode(m_gutterScroll);
  m_gutterRects = std::make_unique<ColorBatch>(window);
  m_gutterLabels = new QSGNode;
  m_gutterImages = std::make_unique<ImageBatch>(window);
  m_gutterScroll->appendChildNode(m_gutterRects->node());
  m_gutterScroll->appendChildNode(m_gutterLabels);
  m_gutterScroll->appendChildNode(m_gutterImages->node());
  m_labelPool.parent = m_gutterLabels;
}

void EditorScene::ColorBatch::update(const QList<GutterRect> &rects, qreal lh, qsizetype originRow) {
  QList<QRectF> geometry;
  geometry.reserve(rects.size());
  for (const GutterRect &r : rects)
    geometry.append(QRectF(r.x0, double(r.row - originRow) * lh + r.y, r.x1 - r.x0, r.height < 0 ? lh : r.height));
  while (m_nodes.size() > rects.size())
    delete m_nodes.takeLast();
  while (m_nodes.size() < rects.size()) {
    m_nodes.append(m_window->createRectangleNode());
    m_group->appendChildNode(m_nodes.last());
  }
  m_rects.resize(rects.size());
  m_colors.resize(rects.size());
  for (qsizetype i = 0; i < rects.size(); ++i) {
    // Nodes beyond the previous count are new and untouched; compare against what they were given.
    if (m_rects[i] != geometry[i] || m_nodes[i]->rect() != geometry[i])
      m_nodes[i]->setRect(geometry[i]);
    if (m_colors[i] != rects[i].color || m_nodes[i]->color() != rects[i].color)
      m_nodes[i]->setColor(rects[i].color);
    m_rects[i] = geometry[i];
    m_colors[i] = rects[i].color;
  }
}

void EditorScene::ImageBatch::update(const QList<GutterImage> &images, qreal lh, qsizetype originRow) {
  while (m_nodes.size() > images.size()) {
    delete m_nodes.takeLast();
    m_keys.removeLast();
    m_rects.removeLast();
  }
  for (qsizetype i = 0; i < images.size(); ++i) {
    const GutterImage &image = images[i];
    const QRectF rect = image.rect.translated(0, double(image.row - originRow) * lh);
    if (i >= m_nodes.size()) {
      // A node needs its texture and rectangle before it joins the tree: the software renderer
      // reads both when it first sees the node.
      QSGImageNode *node = m_window->createImageNode();
      node->setTexture(m_window->createTextureFromImage(image.image));
      node->setOwnsTexture(true);
      node->setRect(rect);
      m_group->appendChildNode(node);
      m_nodes.append(node);
      m_keys.append(image.image.cacheKey());
      m_rects.append(rect);
      continue;
    }
    if (m_keys[i] != image.image.cacheKey()) {
      m_nodes[i]->setTexture(m_window->createTextureFromImage(image.image));
      m_keys[i] = image.image.cacheKey();
    }
    if (m_rects[i] != rect) {
      m_nodes[i]->setRect(rect);
      m_rects[i] = rect;
    }
  }
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
  changed |= m_chipBatch->update(rects(p.chips), p.chipColor);
  changed |= m_chipDotBatch->update(rects(p.chipDots), p.chipDotColor);
  changed |= m_cursorBatch->update(rects(p.cursors), p.cursorColor);
  if (changed)
    ++m_stats.overlayUpdates;
  m_cursorFade->setOpacity(p.cursorVisible ? 1.0 : 0.0);
}

EditorScene::~EditorScene() {
  // Attached items belong to the tree and die with it; pooled ones are ours to delete.
  for (RowPool *pool : {&m_textPool, &m_labelPool}) {
    for (auto &[row, item] : pool->active)
      delete item;
    for (Item *item : std::as_const(pool->free)) {
      delete item->transform; // takes the text node with it
      delete item;
    }
  }
}

EditorScene::Item *EditorScene::acquire(RowPool &pool) {
  Item *item;
  if (!pool.free.isEmpty()) {
    item = pool.free.takeLast();
    ++m_stats.nodesRecycled;
  } else {
    item = new Item;
    item->transform = new QSGTransformNode;
    item->text = m_window->createTextNode();
    item->text->setRenderType(m_renderType);
    item->transform->appendChildNode(item->text);
    ++m_stats.nodesCreated;
  }
  if (!item->attached) {
    pool.parent->appendChildNode(item->transform);
    item->attached = true;
  }
  return item;
}

void EditorScene::release(RowPool &pool, Item *item) {
  pool.parent->removeChildNode(item->transform);
  item->attached = false;
  item->row = -1;
  item->layoutId = 0;
  if (pool.free.size() >= kMaxFree) {
    delete item->transform;
    delete item;
    return;
  }
  pool.free.append(item);
}

// Nodes bake the render type in when a layout is added, so every node that holds text is
// refilled. Free nodes are detached and hold stale glyphs but are always refilled before reuse.
void EditorScene::applyRenderType(QSGTextNode::RenderType type) {
  m_renderType = type;
  for (RowPool *pool : {&m_textPool, &m_labelPool}) {
    for (auto &[row, item] : pool->active) {
      item->text->setRenderType(type);
      item->layoutId = 0;
    }
    for (Item *item : std::as_const(pool->free))
      item->text->setRenderType(type);
  }
}

// Puts a row's transform where its row is, relative to the current origin.
void EditorScene::positionItem(Item *item, qsizetype row, const FrameParams &p) {
  if (item->originRow == m_originRow && item->lineHeight == p.lineHeight && item->layoutId != 0)
    return;
  QMatrix4x4 m;
  m.translate(0, float(double(row - m_originRow) * p.lineHeight));
  item->transform->setMatrix(m);
  item->originRow = m_originRow;
  item->lineHeight = p.lineHeight;
  ++m_stats.matrixUpdates;
}

void EditorScene::sync(const FrameParams &p) {
  if (p.renderType != m_renderType)
    applyRenderType(p.renderType);
  m_background->setRect(p.viewport);
  m_background->setColor(p.background);

  // Text lives right of the gutter.
  QRectF textViewport = p.viewport;
  textViewport.setLeft(p.viewport.left() + p.gutterWidth);
  setClipRect(m_clip, textViewport);

  const QList<FramePlanRow> &rows = *p.rows;
  if (rows.isEmpty()) {
    for (auto &[row, item] : m_textPool.active)
      release(m_textPool, item);
    m_textPool.active.clear();
    syncOverlays(p);
    syncGutter(p);
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
  scroll.translate(float(p.gutterWidth - p.contentX), float(double(m_originRow) * p.lineHeight - p.contentY));
  m_scroll->setMatrix(scroll);

  // Rows that left the plan hand their nodes back to the pool before new rows ask for them.
  for (auto it = m_textPool.active.begin(); it != m_textPool.active.end();) {
    if (it->first < firstRow || it->first > lastRow) {
      release(m_textPool, it->second);
      it = m_textPool.active.erase(it);
    } else {
      ++it;
    }
  }

  const bool colorChanged = m_textColor != p.foreground;
  m_textColor = p.foreground;

  for (const FramePlanRow &planRow : rows) {
    Item *item;
    const auto found = m_textPool.active.find(planRow.row);
    if (found != m_textPool.active.end()) {
      item = found->second;
    } else {
      item = acquire(m_textPool);
      item->row = planRow.row;
      m_textPool.active.emplace(planRow.row, item);
    }
    positionItem(item, planRow.row, p);

    if (item->layoutId != planRow.layout->id || colorChanged) {
      item->text->clear();
      item->text->setColor(p.foreground);
      item->text->addTextLayout(QPointF(planRow.layout->indentX, 0), planRow.layout->layout.get());
      item->layoutId = planRow.layout->id;
      ++m_stats.linesFilled;
    }
  }

  syncOverlays(p);
  syncGutter(p);
}

void EditorScene::syncGutter(const FrameParams &p) {
  if (p.gutterWidth <= 0 || !p.gutter) {
    // No gutter: nothing may be left over from when there was one.
    m_gutterBackground->setRect(QRectF());
    setClipRect(m_gutterClip, QRectF());
    m_gutterRects->update({}, p.lineHeight, m_originRow);
    m_gutterImages->update({}, p.lineHeight, m_originRow);
    for (auto &[row, item] : m_labelPool.active)
      release(m_labelPool, item);
    m_labelPool.active.clear();
    return;
  }
  const QRectF area(p.viewport.left(), p.viewport.top(), p.gutterWidth, p.viewport.height());
  m_gutterBackground->setRect(area);
  m_gutterBackground->setColor(p.gutterBackground);
  setClipRect(m_gutterClip, area);

  QMatrix4x4 scroll;
  scroll.translate(0, float(double(m_originRow) * p.lineHeight - p.contentY));
  m_gutterScroll->setMatrix(scroll);

  m_gutterRects->update(p.gutter->rects, p.lineHeight, m_originRow);
  m_gutterImages->update(p.gutter->images, p.lineHeight, m_originRow);

  ++m_frame;
  for (const GutterLabel &label : p.gutter->labels) {
    Item *item;
    const auto found = m_labelPool.active.find(label.row);
    if (found != m_labelPool.active.end()) {
      item = found->second;
    } else {
      item = acquire(m_labelPool);
      item->row = label.row;
      m_labelPool.active.emplace(label.row, item);
    }
    item->frame = m_frame;
    positionItem(item, label.row, p);
    if (item->layoutId != label.layout->id || item->x != label.x || item->color != label.color) {
      item->text->clear();
      item->text->setColor(label.color);
      item->text->addTextLayout(QPointF(label.x, 0), label.layout->layout.get());
      item->layoutId = label.layout->id;
      item->x = label.x;
      item->color = label.color;
      ++m_stats.linesFilled;
    }
  }
  // Labels not drawn this frame give their nodes back.
  for (auto it = m_labelPool.active.begin(); it != m_labelPool.active.end();) {
    if (it->second->frame != m_frame) {
      release(m_labelPool, it->second);
      it = m_labelPool.active.erase(it);
    } else {
      ++it;
    }
  }
}

} // namespace qce
