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

namespace qce {

// One row of a frame: which display row, and the layout to draw it from.
struct FramePlanRow {
  qsizetype row = 0;
  std::shared_ptr<LineLayout> layout;
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
};

struct SceneStats {
  quint64 nodesCreated = 0;  // QSGTextNode + transform pairs allocated
  quint64 nodesRecycled = 0; // pairs re-pointed at a different row
  quint64 linesFilled = 0;   // addTextLayout calls
  quint64 matrixUpdates = 0; // row transforms rewritten
};

// The editor's scene-graph subtree (ADR 0001). Lives on the render thread:
//
//   EditorScene
//    |- background rect
//    '- clip -- scroll transform -- row transform -- text node   (one pair per visible row, pooled)
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

  Item *acquire();
  void release(Item *item);

  QQuickWindow *m_window;
  QSGRectangleNode *m_background = nullptr;
  QSGClipNode *m_clip = nullptr;
  QSGTransformNode *m_scroll = nullptr;
  std::unordered_map<qsizetype, Item *> m_active;
  QList<Item *> m_free;
  qsizetype m_originRow = 0;
  bool m_haveOrigin = false;
  QColor m_textColor;
  SceneStats m_stats;
};

} // namespace qce

#endif // QCE_EDITORSCENE_H
