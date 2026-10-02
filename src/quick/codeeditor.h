#ifndef CODEEDITOR_H
#define CODEEDITOR_H

#include "core/displaymap.h"
#include "core/highlighter.h"
#include "core/textdocument.h"
#include "quick/editorscene.h"
#include "quick/linelayoutcache.h"
#include "quick/textmetrics.h"
#include "quick/theme.h"

#include <QtCore/QUrl>
#include <QtGui/QColor>
#include <QtGui/QFont>
#include <QtQml/qqmlregistration.h>
#include <QtQuick/QQuickItem>

#include <memory>

// The editor item (ADR 0001). The GUI thread owns the document and does all text layout; the scene
// graph is only touched from updatePaintNode().
class CodeEditor : public QQuickItem {
  Q_OBJECT
  QML_ELEMENT
  Q_DISABLE_COPY(CodeEditor)
  Q_PROPERTY(qsizetype lineCount READ lineCount NOTIFY lineCountChanged FINAL)
  Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged FINAL)
  Q_PROPERTY(qreal loadProgress READ loadProgress NOTIFY loadProgressChanged FINAL)
  Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY fontChanged FINAL)
  Q_PROPERTY(qce::Theme *theme READ theme WRITE setTheme NOTIFY themeChanged FINAL)
public:
  explicit CodeEditor(QQuickItem *parent = nullptr);
  ~CodeEditor() override;

  qce::TextDocument *document() { return &m_document; }
  const qce::TextDocument *document() const { return &m_document; }

  qsizetype lineCount() const { return m_document.rope().lineCount(); }
  bool loading() const { return m_document.isLoading(); }
  qreal loadProgress() const { return m_loadProgress; }

  QFont font() const { return m_font; }
  void setFont(const QFont &font);

  // Never null: the editor owns a dark theme until the host assigns one.
  qce::Theme *theme() const { return m_theme; }
  void setTheme(qce::Theme *theme);

  // Source of per-line styles (RENDER-09). Never null: a NullHighlighter until one is set; passing
  // nullptr restores it. The editor does not take ownership.
  qce::Highlighter *highlighter() const { return m_highlighter; }
  void setHighlighter(qce::Highlighter *highlighter);

  const qce::DisplayMap &displayMap() const { return m_map; }
  const qce::TextMetrics &metrics() const { return m_metrics; }

  // Vertical scroll offset in pixels; the scroll model proper arrives with RENDER-02.
  qreal contentY() const { return m_contentY; }
  void setContentY(qreal y);

  // Counters for tests and benchmarks.
  struct RenderStats {
    quint64 layoutsCreated = 0; // QTextLayouts built since the editor was created
    quint64 layoutCacheHits = 0;
    qsizetype layoutsCached = 0;
    qsizetype rowsInPlan = 0; // rows laid out for the current frame (viewport plus margin)
    qce::SceneStats scene;    // scene-graph node pool activity, as of the last synced frame
  };
  RenderStats renderStats() const;

  // Replaces the whole text. Convenience for small documents; large ones go through load().
  Q_INVOKABLE void setText(const QString &text);
  // Loads a file in the background; the first lines show while the rest is read. Accepts a local
  // file URL or a plain path.
  Q_INVOKABLE void load(const QUrl &file);

signals:
  void lineCountChanged();
  void loadingChanged();
  void loadProgressChanged();
  void fontChanged();
  void themeChanged();
  void loadFailed(const QString &error);

protected:
  void updatePolish() override;
  QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
  void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
  void onDocumentReset();
  void onDocumentChanged(const qce::TextChange &change);

  // One row of the frame: which row, and the layout to draw it from. Built on the GUI thread in
  // updatePolish(), read by updatePaintNode() while the GUI thread is blocked.
  std::shared_ptr<qce::LineLayout> layoutForLine(qsizetype line, const qce::TextSnapshot &snapshot);
  void invalidateLayouts();

  qce::TextDocument m_document;
  qce::DisplayMap m_map{&m_document};
  qce::TextMetrics m_metrics;
  qce::LineLayoutCache m_layouts;
  QList<qce::FramePlanRow> m_plan;
  qce::SceneStats m_sceneStats; // copied from the scene on the render thread during sync
  qreal m_contentY = 0;
  QFont m_font;
  void onThemeChanged();
  void onHighlightInvalidated(qsizetype firstLine, qsizetype lastLine);

  qce::Highlighter *m_highlighter = nullptr;
  qce::NullHighlighter *m_nullHighlighter = nullptr;

  qce::Theme *m_theme = nullptr;
  qce::Theme *m_ownedTheme = nullptr;
  qreal m_loadProgress = 0;
  qsizetype m_lastLineCount = 1;
};

#endif // CODEEDITOR_H
