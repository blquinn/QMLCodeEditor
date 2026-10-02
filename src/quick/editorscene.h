#ifndef QCE_EDITORSCENE_H
#define QCE_EDITORSCENE_H

#include "quick/linelayoutcache.h"

#include <QtCore/QList>
#include <QtGui/QColor>
#include <QtQuick/QSGNode>

#include <memory>
#include <unordered_map>

class QQuickWindow;
class QSGRectangleNode;
class QSGClipNode;
class QSGTransformNode;
class QSGTextNode;
class QSGOpacityNode;

namespace qce {

// One row of a frame: which display row, and the layout to draw it from.
struct FramePlanRow {
  qsizetype row = 0;
  std::shared_ptr<LineLayout> layout;
};

// A horizontal run on one display row, in content coordinates.
struct RowSpan {
  qsizetype row = 0;
  qreal x0 = 0;
  qreal x1 = 0;
};

// What the scene needs from the editor for one frame. Built on the GUI thread, consumed in
// updatePaintNode() while the GUI thread is blocked.
struct FrameParams {
  QRectF viewport;
  QColor background;
  QColor foreground;
  qreal lineHeight = 16;
  qreal contentX = 0;
  qreal contentY = 0;
  const QList<FramePlanRow> *rows = nullptr; // contiguous, ascending

  // Overlays, only for rows in the plan (RENDER-05).
  QColor currentLineColor;
  QColor selectionColor;
  QColor cursorColor;
  const QList<RowSpan> *currentLine = nullptr; // at most one span; empty when a selection exists
  const QList<RowSpan> *selection = nullptr;
  bool hasCursor = false;
  RowSpan cursor;            // x0 is the cursor's x; x1 - x0 its width
  bool cursorVisible = true; // the blink phase; drawn through opacity so a blink costs no geometry
};

struct SceneStats {
  quint64 nodesCreated = 0;   // QSGTextNode + transform pairs allocated
  quint64 nodesRecycled = 0;  // pairs re-pointed at a different row
  quint64 linesFilled = 0;    // addTextLayout calls
  quint64 matrixUpdates = 0;  // row transforms rewritten
  quint64 overlayUpdates = 0; // times the overlay geometry was rebuilt
};

// The editor's scene-graph subtree (ADR 0001). Lives on the render thread:
//
//   EditorScene
//    |- background rect
//    '- clip -- scroll transform -+- backdrop: current line, selection rects
//                                 |- rows: row transform -- text node   (one pair per row, pooled)
//                                 '- opacity -- cursor rect
//
// Scrolling only rewrites the scroll transform. A row that stays on screen keeps its nodes; rows
// that scroll out donate theirs to rows that scroll in. Row transforms are relative to an origin
// row so float matrices stay precise in documents millions of pixels tall.
class EditorScene : public QSGNode {
public:
  EditorScene(QQuickWindow *window);
  ~EditorScene() override;

  void sync(const FrameParams &params);
  const SceneStats &stats() const { return m_stats; }

private:
  struct Item {
    QSGTransformNode *transform = nullptr;
    QSGTextNode *text = nullptr;
    qsizetype row = -1;
    quint64 layoutId = 0;
    qsizetype originRow = 0; // origin the transform was computed against
    bool attached = false;
  };

  // A group of same-colored rectangles, one QSGRectangleNode each. Rectangle nodes are drawn
  // natively on every backend (including the software one) and the renderer batches them
  // because they share a material.
  class RectBatch {
  public:
    explicit RectBatch(QQuickWindow *window) : m_window(window) {
      m_group.setFlag(QSGNode::OwnedByParent, false);
    }
    QSGNode *node() { return &m_group; }
    // Returns false when nothing changed.
    bool update(const QList<QRectF> &rects, const QColor &color);

  private:
    QQuickWindow *m_window;
    QSGNode m_group;
    QList<QSGRectangleNode *> m_nodes;
    QList<QRectF> m_rects;
    QColor m_color;
  };
  void syncOverlays(const FrameParams &params);

  Item *acquire();
  void release(Item *item);

  QQuickWindow *m_window;
  QSGRectangleNode *m_background = nullptr;
  QSGClipNode *m_clip = nullptr;
  QSGTransformNode *m_scroll = nullptr;
  QSGNode *m_backdrop = nullptr;
  QSGNode *m_rows = nullptr;
  QSGOpacityNode *m_cursorFade = nullptr;
  std::unique_ptr<RectBatch> m_currentLineBatch, m_selectionBatch, m_cursorBatch;
  std::unordered_map<qsizetype, Item *> m_active;
  QList<Item *> m_free;
  qsizetype m_originRow = 0;
  bool m_haveOrigin = false;
  QColor m_textColor;
  SceneStats m_stats;
};

} // namespace qce

#endif // QCE_EDITORSCENE_H
