#ifndef CODEEDITOR_H
#define CODEEDITOR_H

#include "core/displaymap.h"
#include "core/foldprovider.h"
#include "core/highlighter.h"
#include "core/commands.h"
#include "core/cursorlayout.h"
#include "core/inputhandler.h"
#include "core/selection.h"
#include "core/selectionset.h"
#include "core/textdocument.h"
#include "quick/editorscene.h"
#include "quick/gutter.h"
#include "quick/linelayoutcache.h"
#include "quick/textmetrics.h"
#include "quick/theme.h"

#include <QtCore/QTimer>
#include <QtGui/QClipboard>
#include <QtCore/QUrl>
#include <QtGui/QColor>
#include <QtGui/QFont>
#include <QtGui/QTextLayout>
#include <QtQml/QQmlListProperty>
#include <QtQml/qqmlregistration.h>
#include <QtQuick/QQuickItem>

#include <memory>

// The editor item (ADR 0001). The GUI thread owns the document and does all text layout; the scene
// graph is only touched from updatePaintNode().
namespace qce {
// Lets QML assign any Highlighter (a SyntaxHighlighter, a host's own) to CodeEditor::highlighter.
struct HighlighterForeign {
  Q_GADGET
  QML_FOREIGN(qce::Highlighter)
  QML_ANONYMOUS
};
// Same for FoldProvider (CodeEditor::foldProvider).
struct FoldProviderForeign {
  Q_GADGET
  QML_FOREIGN(qce::FoldProvider)
  QML_ANONYMOUS
};
} // namespace qce

class CodeEditor : public QQuickItem {
  Q_OBJECT
  QML_ELEMENT
  Q_DISABLE_COPY(CodeEditor)
public:
  // How long lines are shown (M4). Wrapping is soft: the text is not changed.
  enum WrapMode {
    NoWrap,         // one row per line; long lines scroll sideways
    WrapAtViewport, // rows are as wide as the item
    WrapAtColumn    // rows are `wrapColumn` characters wide
  };
  Q_ENUM(WrapMode)

  // What cursor movement does at a folded region (M7).
  enum FoldCursorPolicy {
    SkipFolds,    // movement steps over folded lines; the fold stays closed
    UnfoldOnEnter // movement into a fold opens it
  };
  Q_ENUM(FoldCursorPolicy)

  Q_PROPERTY(qsizetype lineCount READ lineCount NOTIFY lineCountChanged FINAL)
  Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged FINAL)
  Q_PROPERTY(qreal loadProgress READ loadProgress NOTIFY loadProgressChanged FINAL)
  Q_PROPERTY(qreal contentX READ contentX WRITE setContentX NOTIFY contentXChanged FINAL)
  Q_PROPERTY(qreal contentY READ contentY WRITE setContentY NOTIFY contentYChanged FINAL)
  Q_PROPERTY(qreal contentWidth READ contentWidth NOTIFY contentWidthChanged FINAL)
  Q_PROPERTY(qreal contentHeight READ contentHeight NOTIFY contentHeightChanged FINAL)
  Q_PROPERTY(
    qsizetype cursorPosition READ cursorPosition WRITE setCursorPosition NOTIFY selectionChanged FINAL
  )
  Q_PROPERTY(
    bool insertSpaces READ insertSpaces WRITE setInsertSpaces NOTIFY insertSpacesChanged FINAL
  )
  Q_PROPERTY(int indentWidth READ indentWidth WRITE setIndentWidth NOTIFY indentWidthChanged FINAL)
  Q_PROPERTY(qsizetype cursorLine READ cursorLine NOTIFY selectionChanged FINAL)
  Q_PROPERTY(qsizetype cursorColumn READ cursorColumn NOTIFY selectionChanged FINAL)
  Q_PROPERTY(int undoLimit READ undoLimit WRITE setUndoLimit NOTIFY undoLimitChanged FINAL)
  Q_PROPERTY(bool readOnly READ readOnly WRITE setReadOnly NOTIFY readOnlyChanged FINAL)
  Q_PROPERTY(bool canUndo READ canUndo NOTIFY canUndoChanged FINAL)
  Q_PROPERTY(bool canRedo READ canRedo NOTIFY canRedoChanged FINAL)
  Q_PROPERTY(qsizetype selectionStart READ selectionStart NOTIFY selectionChanged FINAL)
  Q_PROPERTY(qsizetype selectionEnd READ selectionEnd NOTIFY selectionChanged FINAL)
  Q_PROPERTY(
    int cursorBlinkInterval READ cursorBlinkInterval WRITE setCursorBlinkInterval NOTIFY
      cursorBlinkIntervalChanged FINAL
  )
  Q_PROPERTY(bool cursorVisible READ cursorVisible NOTIFY cursorVisibleChanged FINAL)
  Q_PROPERTY(int tabWidth READ tabWidth WRITE setTabWidth NOTIFY tabWidthChanged FINAL)
  Q_PROPERTY(
    bool showWhitespace READ showWhitespace WRITE setShowWhitespace NOTIFY showWhitespaceChanged FINAL
  )
  Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY fontChanged FINAL)
  Q_PROPERTY(qce::Theme *theme READ theme WRITE setTheme NOTIFY themeChanged FINAL)
  Q_PROPERTY(qce::Highlighter *highlighter READ highlighter WRITE setHighlighter NOTIFY highlighterChanged FINAL)
  Q_PROPERTY(WrapMode wrapMode READ wrapMode WRITE setWrapMode NOTIFY wrapModeChanged FINAL)
  Q_PROPERTY(int wrapColumn READ wrapColumn WRITE setWrapColumn NOTIFY wrapColumnChanged FINAL)
  Q_PROPERTY(bool wordWrap READ wordWrap WRITE setWordWrap NOTIFY wordWrapChanged FINAL)
  Q_PROPERTY(bool wrapIndent READ wrapIndent WRITE setWrapIndent NOTIFY wrapIndentChanged FINAL)
  Q_PROPERTY(int wrapIndentExtra READ wrapIndentExtra WRITE setWrapIndentExtra NOTIFY wrapIndentExtraChanged FINAL)
  Q_PROPERTY(bool wrapping READ wrapping NOTIFY wrappingChanged FINAL)
  Q_PROPERTY(qce::FoldProvider *foldProvider READ foldProvider WRITE setFoldProvider NOTIFY foldProviderChanged FINAL)
  Q_PROPERTY(
    FoldCursorPolicy foldCursorPolicy READ foldCursorPolicy WRITE setFoldCursorPolicy NOTIFY foldCursorPolicyChanged FINAL
  )
  Q_PROPERTY(QQmlListProperty<qce::GutterColumn> gutterColumns READ gutterColumns FINAL)
  Q_PROPERTY(qreal gutterWidth READ gutterWidth NOTIFY gutterWidthChanged FINAL)
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

  // Tab stops every `tabWidth` cells (default 4).
  int tabWidth() const { return m_metrics.tabWidth(); }
  void setTabWidth(int columns);
  // Draws tabs and spaces as visible marks in the theme's whitespace color.
  bool showWhitespace() const { return m_showWhitespace; }
  void setShowWhitespace(bool show);

  // Never null: the editor owns a dark theme until the host assigns one.
  qce::Theme *theme() const { return m_theme; }
  void setTheme(qce::Theme *theme);

  // Source of per-line styles (RENDER-09). Never null: a NullHighlighter until one is set; passing
  // nullptr restores it. The editor does not take ownership.
  qce::Highlighter *highlighter() const { return m_highlighter; }
  void setHighlighter(qce::Highlighter *highlighter);

  // Soft wrap (M4). With WrapAtViewport rows follow the item's width; WrapAtColumn wraps at a fixed
  // number of characters. Lines break after whitespace (`wordWrap`) or between any two characters;
  // `wrapIndent` starts continuation rows under the line's own indentation, `wrapIndentExtra` adds
  // columns to that. Changing any of them re-wraps the viewport at once and everything else on a
  // worker thread, keeping the text at the top of the view where it is.
  WrapMode wrapMode() const { return m_wrapMode; }
  void setWrapMode(WrapMode mode);
  int wrapColumn() const { return m_wrapColumn; }
  void setWrapColumn(int column);
  bool wordWrap() const { return m_wordWrap; }
  void setWordWrap(bool word);
  bool wrapIndent() const { return m_wrapIndent; }
  void setWrapIndent(bool indent);
  int wrapIndentExtra() const { return m_wrapIndentExtra; }
  void setWrapIndentExtra(int columns);
  // True while a worker is still refining row counts, i.e. contentHeight is an estimate.
  bool wrapping() const { return m_wrapping; }

  // The gutter (M5): columns laid out left to right in the order given, to the left of the text. The
  // text area is `width - gutterWidth` wide; wrap, hit-testing and scrolling all use that. The editor
  // does not own columns it is handed through the C++ API.
  QQmlListProperty<qce::GutterColumn> gutterColumns();
  const QList<qce::GutterColumn *> &gutterColumnList() const { return m_columns; }
  void addGutterColumn(qce::GutterColumn *column);
  void removeGutterColumn(qce::GutterColumn *column);
  qreal gutterWidth() const { return m_gutterWidth; }
  // Width of the area text is drawn in.
  qreal textViewportWidth() const { return qMax<qreal>(0, width() - m_gutterWidth); }
  // What the columns painted for the last frame plan.
  const qce::GutterPlan &gutterPlan() const { return m_gutter; }

  // Folding (M7). The provider says what can be folded; the default folds by indentation, and a
  // SyntaxHighlighter's `folds` follows its grammar. Folds hide whole lines after a header line, keep
  // following edits, and are opened by anything that puts the cursor inside them (except when the
  // cursor policy is SkipFolds and the move was by keyboard, which steps over them instead).
  // Never null: passing nullptr restores the default. The editor does not take ownership.
  qce::FoldProvider *foldProvider() const { return m_foldProvider; }
  void setFoldProvider(qce::FoldProvider *provider);
  FoldCursorPolicy foldCursorPolicy() const { return m_foldPolicy; }
  void setFoldCursorPolicy(FoldCursorPolicy policy);
  // Fold ranges whose header line is in [firstLine, lastLine], for gutter columns.
  QList<qce::FoldRange> foldRangesIn(qsizetype firstLine, qsizetype lastLine);
  // All return whether anything changed. Lines are zero-based buffer lines.
  Q_INVOKABLE bool fold(qsizetype line);
  Q_INVOKABLE bool unfold(qsizetype line);
  Q_INVOKABLE bool toggleFold(qsizetype line);
  Q_INVOKABLE bool isFolded(qsizetype line) const;
  // Fold the innermost region around the cursor that is open / open the fold at the cursor.
  Q_INVOKABLE bool foldAtCursor();
  Q_INVOKABLE bool unfoldAtCursor();
  Q_INVOKABLE bool foldAll();
  Q_INVOKABLE bool unfoldAll();
  // Folds the regions of nesting level `level` (1 = outermost) and opens the rest.
  Q_INVOKABLE bool foldToLevel(int level);

  const qce::DisplayMap &displayMap() const { return m_map; }
  const qce::TextMetrics &metrics() const { return m_metrics; }

  // Scroll model (RENDER-02). contentX/contentY are the pixel offset of the viewport's top-left
  // corner inside the content and are clamped to [0, contentSize - viewportSize], so a QML
  // ScrollBar can bind to them like it would to a Flickable. Scrolling only moves a scene-graph
  // transform; text is laid out again only when new rows enter the layout window.
  qreal contentX() const { return m_contentX; }
  qreal contentY() const { return m_contentY; }
  void setContentX(qreal x);
  void setContentY(qreal y);
  // Height is exact: rows * line height. Width is the widest line laid out so far (a document
  // is never measured whole), so it grows as the user scrolls through long lines.
  qreal contentWidth() const { return m_contentWidth; }
  qreal contentHeight() const { return m_contentHeight; }

  // The selections (ADR 0005). cursorPosition/selectionStart/selectionEnd describe the primary one;
  // cursorPosition is its head. They follow edits.
  qce::SelectionSet *selections() { return &m_selections; }
  qsizetype cursorPosition() const;
  void setCursorPosition(qsizetype offset);
  // Zero-based line and UTF-16 column of the primary cursor.
  qsizetype cursorLine() const;
  qsizetype cursorColumn() const;
  qsizetype selectionStart() const;
  qsizetype selectionEnd() const;
  // Selects [anchor, head] with the cursor at `head`.
  Q_INVOKABLE void select(qsizetype anchor, qsizetype head);
  // Edits from the user (keys, paste, input methods) are refused while read-only; the document API
  // and setText()/load() still work.
  // Tab inserts `indentWidth` columns of spaces (or a tab character when false); selections of
  // several lines are indented by one level.
  bool insertSpaces() const { return m_insertSpaces; }
  void setInsertSpaces(bool spaces);
  int indentWidth() const { return m_indentWidth; }
  void setIndentWidth(int columns);
  bool readOnly() const { return m_readOnly; }
  void setReadOnly(bool readOnly);
  // Most undo steps kept; the oldest are dropped beyond it. 0 (the default) keeps them all.
  int undoLimit() const { return m_undoLimit; }
  void setUndoLimit(int steps);
  bool canUndo() const { return m_canUndo; }
  bool canRedo() const { return m_canRedo; }

  // The handler that turns key events into commands (ADR 0010). Never null: the default keymap
  // until the host sets another; passing nullptr restores it. The editor does not take ownership.
  qce::InputHandler *inputHandler() const { return m_handler; }
  void setInputHandler(qce::InputHandler *handler);

  // Commands for hosts and menus. Each scrolls the cursor into view.
  Q_INVOKABLE void undo();
  Q_INVOKABLE void redo();
  Q_INVOKABLE void selectAll();
  Q_INVOKABLE void copy();
  Q_INVOKABLE void cut();
  Q_INVOKABLE void paste();
  // Replaces every selection with `text`.
  Q_INVOKABLE void insert(const QString &text);
  // Scrolls the minimum distance that shows the primary cursor.
  Q_INVOKABLE void ensureCursorVisible();

  // Time in ms between blink phases; 0 keeps the cursor solid. Any cursor movement shows it.
  int cursorBlinkInterval() const { return m_blinkTimer.interval(); }
  void setCursorBlinkInterval(int ms);
  // The blink phase; the cursor is drawn only while the editor also has focus. Blinking stops (the
  // cursor stays solid) after 10 s without input, and any cursor movement restarts it.
  bool cursorVisible() const { return m_cursorVisible; }
  bool hasFocus() const { return m_hasFocus; }

  // Input-method queries: the cursor line as surrounding text, and the cursor rectangle (inside the
  // composition when there is one) for placing candidate windows.
  QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

  // Item coordinates -> buffer offset (like TextEdit.positionAt), and back: the rectangle of the
  // character cell at `offset` in item coordinates. Both go through the display map.
  Q_INVOKABLE qsizetype positionAt(qreal x, qreal y);
  Q_INVOKABLE QRectF rectForPosition(qsizetype offset);

  // Counters for tests and benchmarks.
  struct RenderStats {
    quint64 layoutsCreated = 0; // QTextLayouts built since the editor was created
    quint64 layoutCacheHits = 0;
    qsizetype layoutsCached = 0;
    qsizetype rowsInPlan = 0; // rows laid out for the current frame (viewport plus margin)
    quint64 polishCalls = 0; // updatePolish() runs that rebuilt the plan or checked it
    quint64 polishNs = 0;    // total time spent in updatePolish()
    quint64 polishMaxNs = 0; // slowest single run
    qce::SceneStats scene;    // scene-graph node pool activity, as of the last synced frame
  };
  RenderStats renderStats() const;
  // Forgets the polish timings so a benchmark scenario reads its own slowest run.
  void resetPolishStats() { m_polishCalls = m_polishNs = m_polishMaxNs = 0; }

  // Replaces the whole text. Convenience for small documents; large ones go through load().
  Q_INVOKABLE void setText(const QString &text);
  // Loads a file in the background; the first lines show while the rest is read. Accepts a local
  // file URL or a plain path.
  Q_INVOKABLE void load(const QUrl &file);
  // Writes the text to `file` in the format it was loaded with, on a worker thread (the text is a
  // snapshot, so editing can continue). Reports through saved() or saveFailed().
  Q_INVOKABLE void save(const QUrl &file);

signals:
  void lineCountChanged();
  void loadingChanged();
  void loadProgressChanged();
  void fontChanged();
  void tabWidthChanged();
  void showWhitespaceChanged();
  void selectionChanged();
  void cursorBlinkIntervalChanged();
  void cursorVisibleChanged();
  void readOnlyChanged();
  void undoLimitChanged();
  void insertSpacesChanged();
  void indentWidthChanged();
  void canUndoChanged();
  void canRedoChanged();
  void contentXChanged();
  void contentYChanged();
  void contentWidthChanged();
  void contentHeightChanged();
  void themeChanged();
  void highlighterChanged();
  void wrapModeChanged();
  void wrapColumnChanged();
  void wordWrapChanged();
  void wrapIndentChanged();
  void wrapIndentExtraChanged();
  void wrappingChanged();
  void gutterWidthChanged();
  void foldProviderChanged();
  void foldCursorPolicyChanged();
  void loadFailed(const QString &error);
  void saved(const QString &path);
  void saveFailed(const QString &error);

protected:
  void updatePolish() override;
  void keyPressEvent(QKeyEvent *event) override;
  void inputMethodEvent(QInputMethodEvent *event) override;
  void focusInEvent(QFocusEvent *event) override;
  void focusOutEvent(QFocusEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void mouseUngrabEvent() override;
  void hoverMoveEvent(QHoverEvent *event) override;
  void hoverLeaveEvent(QHoverEvent *event) override;
  void wheelEvent(QWheelEvent *event) override;
  QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
  void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
  class EditorLayout;
  class EditorHost;
  enum class DragUnit : quint8 { Char, Word, Line };
  void handlePress(QMouseEvent *event, bool doubleClick);
  void handleGutterPress(QMouseEvent *event, bool doubleClick);
  ulong m_lastGutterPress = 0;
  qce::GutterColumn *columnAt(qreal x) const;
  qce::GutterContext gutterContext() const;
  // Lays the columns out; true when the gutter's width changed.
  bool updateGutterLayout();
  void buildGutter();
  void scrollGutter();
  void updateHover(const QPointF &pos);
  void updateDrag();
  void endDrag();
  void autoScrollDrag();
  // The unit (word or line) around `offset` as [start, end).
  QPair<qsizetype, qsizetype> unitRangeAt(qsizetype offset, DragUnit unit) const;
  void afterCommand();
  // Input-method composition (INPUT-05): shown at the anchored spot, never part of the document.
  bool hasPreedit() const { return m_preeditAnchor != qce::InvalidAnchor; }
  void clearPreedit();
  qreal preeditCursorX(const qce::LineLayout &layout) const;
  void setClipboardFromSelections(QClipboard::Mode mode);
  void pasteFrom(QClipboard::Mode mode);
  int m_pendingPastes = 0;

  QString m_preedit;
  int m_preeditCursor = -1; // position inside the preedit; -1 puts the cursor at its end
  QList<QTextLayout::FormatRange> m_preeditFormats; // relative to the preedit's start
  qce::AnchorId m_preeditAnchor = qce::InvalidAnchor;
  bool m_inImeEvent = false;

  // Mouse selection: the unit the gesture selects by, the range first selected (the fixed end of
  // word and line drags), and where the pointer is, for auto-scroll while it is outside.
  bool m_dragging = false;
  DragUnit m_dragUnit = DragUnit::Char;
  qsizetype m_dragAnchor = 0;
  QPair<qsizetype, qsizetype> m_dragInitial;
  QPointF m_dragPos;
  QTimer m_autoScrollTimer;
  int m_clickCount = 0;
  ulong m_lastClickTime = 0;
  QPointF m_lastClickPos;
  void updateUndoState();
  qsizetype columnForX(const qce::LineLayout &layout, qreal x) const;
  void onDocumentReset();
  void onDocumentChanged(const qce::TextChange &change);

  // One row of the frame: which row, and the layout to draw it from. Built on the GUI thread in
  // updatePolish(), read by updatePaintNode() while the GUI thread is blocked.
  std::shared_ptr<qce::LineLayout> layoutForRow(const qce::DisplayRow &row, const qce::TextSnapshot &snapshot);
  // The row of the display map that shows `position`.
  qce::DisplayRow rowOfPosition(qce::TextPosition position) const;
  void invalidateLayouts();

  qce::TextDocument m_document;
  qce::DisplayMap m_map{&m_document};
  // Folding plumbing. Fold commands run through these so the view keeps its place and the cursor
  // moves off lines that just got hidden.
  void onFoldsChanged();
  bool foldChanged(bool changed);
  bool moveSelectionsOutOfFolds();
  void buildFoldChips();
  void revealCursor();
  void foldCommand(qce::FoldCommand command);
  qce::IndentFoldProvider *m_defaultFolds = nullptr;
  qce::FoldProvider *m_foldProvider = nullptr;
  FoldCursorPolicy m_foldPolicy = SkipFolds;
  // Placeholder chips after folded lines for the rows of the plan, in content coordinates.
  struct ChipHit {
    qsizetype row;
    qreal x0, x1;
    qsizetype line;
  };
  QList<ChipHit> m_chipHits;
  QList<qce::RowSpan> m_chipSpans, m_chipDotSpans;
  qce::GutterColumn *m_hoverColumn = nullptr;
  qsizetype m_hoverLine = -1;
  QList<qce::GutterColumn *> m_columns;
  qce::GutterPlan m_gutter;
  qreal m_gutterWidth = 0;
  bool m_cursorInGutter = false;
  qce::SelectionSet m_selections{&m_document};
  qce::TextMetrics m_metrics;
  qce::LineLayoutCache m_layouts;
  QList<qce::FramePlanRow> m_plan;
  quint64 m_polishCalls = 0, m_polishNs = 0, m_polishMaxNs = 0;
  qce::SceneStats m_sceneStats; // copied from the scene on the render thread during sync
  void updateContentSize();
  void buildOverlays();
  void buildTabMarks();
  void restartBlink();
  // `column` is a column of the buffer line; the layout is one row of it.
  qreal xForColumn(const qce::LineLayout &layout, qsizetype column) const;
  void invalidatePlan();

  // Soft wrap plumbing. Settings only mark the wrap dirty; the next polish applies it once, however
  // many settings or resize events came in.
  void invalidateWrap();
  void applyWrap();
  void updateWrapMeasure();
  qce::WrapConfig wrapConfig() const;
  void onRowsReestimated();
  // The scroll anchor is the buffer position at the top of the view. Row numbers move when wrapping
  // is refined, so contentY is derived from the anchor again whenever they do.
  void captureAnchor();
  void restoreAnchor();
  void updateContentSizeKeepingAnchor();
  struct ScrollAnchor {
    qsizetype line = 0;
    qsizetype column = 0;
    qreal offset = 0; // pixels the view is scrolled into the row
  };
  ScrollAnchor m_anchor;
  WrapMode m_wrapMode = NoWrap;
  int m_wrapColumn = 80;
  bool m_wordWrap = true;
  bool m_wrapIndent = true;
  int m_wrapIndentExtra = 0;
  std::shared_ptr<qce::FontWrapMeasure> m_wrapMeasure;
  bool m_wrapDirty = true;
  bool m_reanchorPending = false;
  bool m_reanchoring = false;
  bool m_wrapping = false;

  qce::EditContext editContext();
  std::unique_ptr<EditorLayout> m_cursorLayout;
  std::unique_ptr<EditorHost> m_host;
  qce::DefaultInputHandler m_defaultHandler;
  qce::InputHandler *m_handler = &m_defaultHandler;
  bool m_readOnly = false;
  int m_undoLimit = 0;
  bool m_insertSpaces = true;
  int m_indentWidth = 4;
  bool m_canUndo = false;
  bool m_canRedo = false;
  void onSelectionsChanged();
  bool m_showWhitespace = false;
  QTimer m_blinkTimer;
  static constexpr int kBlinkTimeoutMs = 10000;
  bool m_cursorVisible = true;
  bool m_hasFocus = false;
  int m_blinkPhases = 0;
  QList<qce::RowSpan> m_currentLineSpans;
  QList<qce::RowSpan> m_selectionSpans;
  QList<qce::RowSpan> m_markSpans;
  QList<qce::RowSpan> m_cursorSpans;
  qce::Selection m_lastSelection;
  int m_lastSelectionCount = 1;

  qreal m_contentX = 0;
  qreal m_contentY = 0;
  qreal m_contentWidth = 0;
  qreal m_contentHeight = 0;
  qreal m_maxLineWidth = 0;
  bool m_planDirty = true;
  qsizetype m_planFirst = -1;
  qsizetype m_planLast = -1;
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
