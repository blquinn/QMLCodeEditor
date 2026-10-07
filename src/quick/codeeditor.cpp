#include "codeeditor.h"
#include "core/bracketmatch.h"

#include "core/filesaver.h"
#include "core/indentation.h"
#include "core/indentguides.h"
#include "core/folding.h"
#include "core/textboundaries.h"
#include "quick/decorationcolumn.h"
#include "quick/popupplacement.h"

#include <QtConcurrent/QtConcurrentRun>
#include <QtCore/QFutureWatcher>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QMimeData>
#include <QtGui/QClipboard>
#include <QtGui/QInputMethod>
#include <QtGui/QInputMethodEvent>
#include <QtGui/QGuiApplication>
#include <QtGui/QStyleHints>
#include <QtGui/QTextOption>
#include <QtCore/QPointF>

#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGRectangleNode>
#include <QtCore/QElapsedTimer>

#include <algorithm>
#include <tuple>
#include <limits>
#include <cmath>

using namespace Qt::StringLiterals;

// Geometry for movement commands, from the same layouts and metrics the editor draws with.
class CodeEditor::EditorLayout final : public qce::CursorLayout {
public:
  explicit EditorLayout(CodeEditor *editor) : m_editor(editor) {}

  qreal xForOffset(qsizetype offset) const override {
    const qce::TextSnapshot snapshot = m_editor->m_document.snapshot();
    const qce::TextPosition pos = m_editor->m_map.visiblePosition(snapshot.rope().positionAt(offset));
    return m_editor->xForColumn(*m_editor->layoutForRow(m_editor->rowOfPosition(pos), snapshot), pos.column);
  }
  qsizetype offsetForX(const qce::DisplayRow &row, qreal x) const override {
    const qce::TextSnapshot snapshot = m_editor->m_document.snapshot();
    const qsizetype column = qBound(
      row.startColumn, m_editor->columnForX(*m_editor->layoutForRow(row, snapshot), x), row.lastCursorColumn()
    );
    const qce::Rope &rope = snapshot.rope();
    return rope.snapToCodePoint(rope.offsetAt({row.line, column}));
  }
  qsizetype pageRows() const override {
    // Leave one row of context, like most editors.
    return qMax<qsizetype>(1, qsizetype(m_editor->height() / m_editor->m_metrics.lineHeight()) - 1);
  }

private:
  CodeEditor *m_editor;
};

class CodeEditor::EditorHost final : public qce::InputHost {
public:
  explicit EditorHost(CodeEditor *editor) : m_editor(editor) {}
  void copy() override { m_editor->copy(); }
  void cut() override { m_editor->cut(); }
  void paste() override { m_editor->paste(); }
  void scrollRows(qsizetype rows) override {
    m_editor->setContentY(m_editor->m_contentY + qreal(rows) * m_editor->m_metrics.lineHeight());
  }
  void foldCommand(qce::FoldCommand command) override { m_editor->foldCommand(command); }
  void gotoDiagnostic(bool forward) override { m_editor->gotoDiagnostic(forward, qce::HintSeverity); }
  VisibleRows visibleRows() const override {
    const qreal lineHeight = m_editor->m_metrics.lineHeight();
    const qsizetype rows = m_editor->m_map.rowCount();
    if (rows <= 0 || lineHeight <= 0)
      return {};
    const qsizetype first = qBound<qsizetype>(0, qsizetype(std::floor(m_editor->m_contentY / lineHeight)), rows - 1);
    const qsizetype last =
      qBound<qsizetype>(first, qsizetype(std::floor((m_editor->m_contentY + m_editor->height() - 1) / lineHeight)), rows - 1);
    return {first, last, true};
  }
  QString clipboardText(bool selection) override {
    QClipboard *clipboard = QGuiApplication::clipboard();
    return clipboard->text(selection && clipboard->supportsSelection() ? QClipboard::Selection : QClipboard::Clipboard);
  }
  void setClipboardText(const QString &text, bool selection) override {
    QClipboard *clipboard = QGuiApplication::clipboard();
    clipboard->setText(text, selection && clipboard->supportsSelection() ? QClipboard::Selection : QClipboard::Clipboard);
  }
  void setSearchHighlight(const QRegularExpression &pattern) override { m_editor->setSearchHighlight(pattern); }

private:
  CodeEditor *m_editor;
};

CodeEditor::CodeEditor(QQuickItem *parent) : QQuickItem(parent) {
  setFlag(ItemHasContents);
  setFlag(ItemIsFocusScope);
  setFlag(ItemAcceptsInputMethod);
  setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton);
  setCursor(Qt::IBeamCursor);
  setAcceptHoverEvents(true);
  m_hoverTimer.setSingleShot(true);
  m_hoverTimer.setInterval(500);
  connect(&m_hoverTimer, &QTimer::timeout, this, &CodeEditor::onHoverTimer);
  m_popupGrace.setSingleShot(true);
  m_popupGrace.setInterval(350);
  connect(&m_popupGrace, &QTimer::timeout, this, [this] {
    // A popup with the keyboard focus holds a selection someone may be copying: it stays.
    if (!m_popupHovered && !popupHasFocus())
      hidePopup();
  });
  m_autoScrollTimer.setInterval(30);
  connect(&m_autoScrollTimer, &QTimer::timeout, this, &CodeEditor::autoScrollDrag);
  m_cursorLayout = std::make_unique<EditorLayout>(this);
  m_host = std::make_unique<EditorHost>(this);
  m_vim = new qce::VimInputHandler(this);
  connect(m_vim, &qce::VimInputHandler::modeChanged, this, [this] {
    if (m_handler == m_vim) {
      QGuiApplication::inputMethod()->update(Qt::ImEnabled | Qt::ImQueryInput);
      invalidatePlan(); // the cursor's shape and character change with the mode
    }
  });
  m_font = qce::TextMetrics::defaultMonospaceFont();
  m_metrics.setFont(m_font);
  updateWrapMeasure();
  connect(&m_map, &qce::DisplayMap::rowsReestimated, this, &CodeEditor::onRowsReestimated);
  connect(&m_map, &qce::DisplayMap::foldsChanged, this, &CodeEditor::onFoldsChanged);
  m_defaultFolds = new qce::IndentFoldProvider(this);
  m_foldProvider = m_defaultFolds;
  connect(m_defaultFolds, &qce::FoldProvider::invalidated, this, &CodeEditor::invalidatePlan);
  connect(&m_map, &qce::DisplayMap::wrapProgress, this, [this](qsizetype left) {
    if ((left > 0) != m_wrapping) {
      m_wrapping = left > 0;
      emit wrappingChanged();
    }
  });
  m_ownedTheme = m_theme = qce::Theme::createDark(this);
  connect(m_theme, &qce::Theme::changed, this, &CodeEditor::onThemeChanged);

  m_highlighter = m_nullHighlighter = new qce::NullHighlighter(this);

  connect(&m_selections, &qce::SelectionSet::changed, this, &CodeEditor::onSelectionsChanged);
  m_blinkTimer.setInterval(530);
  connect(&m_blinkTimer, &QTimer::timeout, this, [this] {
    m_cursorVisible = !m_cursorVisible;
    emit cursorVisibleChanged();
    update();
    // After a while without input the cursor stays solid, so an idle editor stops rendering.
    if (++m_blinkPhases * m_blinkTimer.interval() >= kBlinkTimeoutMs) {
      m_blinkTimer.stop();
      if (!m_cursorVisible) {
        m_cursorVisible = true;
        emit cursorVisibleChanged();
      }
    }
  });
  setActiveFocusOnTab(true);

  connect(&m_decorations, &qce::DecorationSet::changed, this, &CodeEditor::onDecorationsChanged);
  // Inline virtual text widens lines, which soft wrap has to know before the rows are asked for.
  m_map.setDecorations(&m_decorations);
  connect(&m_decorations, &qce::DecorationSet::inlineLinesChanged, &m_map, &qce::DisplayMap::rewrapLines);
  connect(&m_diagnostics, &qce::DiagnosticSet::changed, this, &CodeEditor::diagnosticsChanged);
  connect(&m_document, &qce::TextDocument::textReset, this, &CodeEditor::onDocumentReset);
  connect(&m_document, &qce::TextDocument::changed, this, &CodeEditor::onDocumentChanged);
  connect(&m_document, &qce::TextDocument::loadProgress, this, [this](qint64 done, qint64 total) {
    m_loadProgress = total > 0 ? qreal(done) / qreal(total) : 0;
    emit loadProgressChanged();
  });
  connect(&m_document, &qce::TextDocument::loadFinished, this, [this] {
    m_loadProgress = 1;
    emit loadProgressChanged();
    emit loadingChanged();
    applyDetectedIndentation();
  });
  connect(&m_document, &qce::TextDocument::loadFailed, this, [this](const QString &error) {
    emit loadingChanged();
    emit loadFailed(error);
  });
}

CodeEditor::~CodeEditor() {
  if (m_popup)
    delete m_popup.data(); // lives under the window's content item, not under us
  m_highlighter->detach(); // it may outlive the document that is about to go
  for (qce::Highlighter *overlay : std::as_const(m_overlays)) {
    disconnect(overlay, nullptr, this, nullptr);
    overlay->detach();
  }
  // Columns may outlive the editor (QML owns them); they must let go of the document and items first.
  const QList<qce::GutterColumn *> columns = m_columns;
  m_columns.clear();
  for (qce::GutterColumn *column : columns) {
    disconnect(column, nullptr, this, nullptr);
    column->detach(this);
  }
}

QQmlListProperty<qce::GutterColumn> CodeEditor::gutterColumns() {
  return QQmlListProperty<qce::GutterColumn>(
    this, nullptr,
    [](QQmlListProperty<qce::GutterColumn> *list, qce::GutterColumn *column) {
      static_cast<CodeEditor *>(list->object)->addGutterColumn(column);
    },
    [](QQmlListProperty<qce::GutterColumn> *list) {
      return static_cast<CodeEditor *>(list->object)->m_columns.size();
    },
    [](QQmlListProperty<qce::GutterColumn> *list, qsizetype index) {
      return static_cast<CodeEditor *>(list->object)->m_columns.at(index);
    },
    [](QQmlListProperty<qce::GutterColumn> *list) {
      auto *self = static_cast<CodeEditor *>(list->object);
      const QList<qce::GutterColumn *> columns = self->m_columns;
      for (qce::GutterColumn *column : columns)
        self->removeGutterColumn(column);
    }
  );
}

void CodeEditor::addGutterColumn(qce::GutterColumn *column) {
  if (!column || m_columns.contains(column))
    return;
  m_columns.append(column);
  connect(column, &qce::GutterColumn::contentChanged, this, &CodeEditor::invalidatePlan);
  connect(column, &QObject::destroyed, this, [this, column] {
    if (m_hoverColumn == column)
      m_hoverColumn = nullptr;
    if (m_columns.removeOne(column))
      invalidatePlan();
  });
  column->attach(this);
  invalidatePlan();
}

void CodeEditor::removeGutterColumn(qce::GutterColumn *column) {
  if (!m_columns.removeOne(column))
    return;
  if (m_hoverColumn == column)
    m_hoverColumn = nullptr;
  disconnect(column, nullptr, this, nullptr);
  column->detach(this);
  invalidatePlan();
}

qce::GutterContext CodeEditor::gutterContext() const {
  qce::GutterContext context;
  context.metrics = &m_metrics;
  context.theme = m_theme;
  context.document = &m_document;
  context.map = &m_map;
  context.cursorLine = cursorLine();
  context.lineCount = lineCount();
  context.contentY = m_contentY;
  context.height = height();
  return context;
}

// Places the visible columns side by side. A width only changes when a column's own rules say so (a new
// digit in the line count, a font change), never with scrolling.
bool CodeEditor::updateGutterLayout() {
  const qce::GutterContext context = gutterContext();
  qreal x = 0;
  for (qce::GutterColumn *column : std::as_const(m_columns)) {
    const qreal width = column->isVisible() ? std::ceil(qMax<qreal>(0, column->measure(context))) : 0;
    column->setPlacement(x, width);
    x += width;
  }
  if (x == m_gutterWidth)
    return false;
  m_gutterWidth = x;
  emit gutterWidthChanged();
  return true;
}

void CodeEditor::buildGutter() {
  m_gutter.clear();
  const qce::GutterContext context = gutterContext();
  // The current line's band continues across the gutter.
  if (m_gutterWidth > 0)
    for (const qce::RowSpan &span : std::as_const(m_currentLineSpans))
      m_gutter.rects.append({span.row, 0, m_gutterWidth, 0, -1, m_theme->currentLine()});
  for (qce::GutterColumn *column : std::as_const(m_columns)) {
    if (!column->isVisible())
      continue;
    qce::GutterPainter painter(&m_gutter, column->x(), column->actualWidth());
    column->paintRows(context, m_plan, painter);
  }
}

void CodeEditor::scrollGutter() {
  if (m_columns.isEmpty())
    return;
  const qce::GutterContext context = gutterContext();
  for (qce::GutterColumn *column : std::as_const(m_columns))
    if (column->isVisible())
      column->scrolled(context);
}

qce::GutterColumn *CodeEditor::columnAt(qreal x) const {
  for (qce::GutterColumn *column : m_columns)
    if (column->isVisible() && x >= column->x() && x < column->x() + column->actualWidth())
      return column;
  return nullptr;
}

void CodeEditor::setFont(const QFont &font) {
  if (m_font == font)
    return;
  m_font = font;
  m_metrics.setFont(font);
  updateWrapMeasure();
  invalidateLayouts();
  updateContentSize();
  emit fontChanged();
}

void CodeEditor::setTabWidth(int columns) {
  columns = qBound(1, columns, 32);
  if (columns == m_metrics.tabWidth())
    return;
  m_metrics.setTabWidth(columns);
  m_defaultFolds->setTabWidth(columns);
  updateWrapMeasure();
  invalidateLayouts();
  emit tabWidthChanged();
}

void CodeEditor::setShowWhitespace(bool show) {
  if (show == m_showWhitespace)
    return;
  m_showWhitespace = show;
  invalidateLayouts();
  emit showWhitespaceChanged();
}

QSGTextNode::RenderType CodeEditor::toNodeRenderType(RenderType type) {
  switch (type) {
  case QtRendering:
    return QSGTextNode::QtRendering;
  case NativeRendering:
    return QSGTextNode::NativeRendering;
  case CurveRendering:
    return QSGTextNode::CurveRendering;
  }
  return QSGTextNode::QtRendering;
}

CodeEditor::RenderType CodeEditor::fromWindowRenderType(QQuickWindow::TextRenderType type) {
  switch (type) {
  case QQuickWindow::QtTextRendering:
    return QtRendering;
  case QQuickWindow::NativeTextRendering:
    return NativeRendering;
  case QQuickWindow::CurveTextRendering:
    return CurveRendering;
  }
  return QtRendering;
}

CodeEditor::RenderType CodeEditor::renderType() const {
  return m_renderType.value_or(fromWindowRenderType(QQuickWindow::textRenderType()));
}

// Layout and metrics come from QTextLayout and QFontMetrics, which never see the render type, so
// only the scene needs to hear about it.
void CodeEditor::setRenderType(RenderType type) {
  if (m_renderType == type)
    return;
  const RenderType before = renderType();
  m_renderType = type;
  update();
  if (renderType() != before)
    emit renderTypeChanged();
}

void CodeEditor::resetRenderType() {
  if (!m_renderType)
    return;
  const RenderType before = renderType();
  m_renderType.reset();
  update();
  if (renderType() != before)
    emit renderTypeChanged();
}

void CodeEditor::updateWrapMeasure() {
  m_wrapMeasure = std::make_shared<qce::FontWrapMeasure>(m_metrics.layoutFont(), m_metrics.tabWidth(), m_metrics.cellAdvance());
  invalidateWrap();
}

void CodeEditor::setWrapMode(WrapMode mode) {
  if (mode == m_wrapMode)
    return;
  m_wrapMode = mode;
  invalidateWrap();
  emit wrapModeChanged();
}

void CodeEditor::setWrapColumn(int column) {
  column = qBound(1, column, 1000);
  if (column == m_wrapColumn)
    return;
  m_wrapColumn = column;
  invalidateWrap();
  emit wrapColumnChanged();
}

void CodeEditor::setWordWrap(bool word) {
  if (word == m_wordWrap)
    return;
  m_wordWrap = word;
  invalidateWrap();
  emit wordWrapChanged();
}

void CodeEditor::setWrapIndent(bool indent) {
  if (indent == m_wrapIndent)
    return;
  m_wrapIndent = indent;
  invalidateWrap();
  emit wrapIndentChanged();
}

void CodeEditor::setWrapIndentExtra(int columns) {
  columns = qBound(0, columns, 32);
  if (columns == m_wrapIndentExtra)
    return;
  m_wrapIndentExtra = columns;
  invalidateWrap();
  emit wrapIndentExtraChanged();
}

void CodeEditor::invalidateWrap() {
  m_wrapDirty = true;
  polish();
}

qce::WrapConfig CodeEditor::wrapConfig() const {
  qce::WrapConfig config;
  // An item that has no size yet has nothing to wrap to.
  if (m_wrapMode == NoWrap || (m_wrapMode == WrapAtViewport && textViewportWidth() <= 0))
    return config;
  config.mode = m_wrapMode == WrapAtViewport ? qce::WrapMode::Viewport : qce::WrapMode::Column;
  config.width = m_wrapMode == WrapAtViewport ? textViewportWidth() : 0;
  config.column = m_wrapColumn;
  config.wordBreak = m_wordWrap;
  config.hangingIndent = m_wrapIndent;
  config.extraIndent = m_wrapIndentExtra;
  config.measure = m_wrapMeasure;
  return config;
}

// Makes the display map follow the settings. Cheap when nothing changed (a resize of a column-wrapped
// editor, say); otherwise every line becomes an estimate again and the lines in view are wrapped as
// they are asked for.
void CodeEditor::applyWrap() {
  m_wrapDirty = false;
  const qce::WrapConfig config = wrapConfig();
  if (config == m_map.wrapConfig())
    return;
  m_map.setWrapConfig(config);
  m_layouts.clear();
  m_maxLineWidth = 0;
  m_planDirty = true;
  updateContentSizeKeepingAnchor();
  const bool wrapping = m_map.estimatedLineCount() > 0;
  if (wrapping != m_wrapping) {
    m_wrapping = wrapping;
    emit wrappingChanged();
  }
}

void CodeEditor::onRowsReestimated() {
  m_reanchorPending = true;
  invalidatePlan();
}

void CodeEditor::captureAnchor() {
  const qsizetype row = qBound<qsizetype>(0, qsizetype(std::floor(m_contentY / m_metrics.lineHeight())), m_map.rowCount() - 1);
  const qce::DisplayRow displayRow = m_map.rowAt(row);
  m_anchor = {displayRow.line, displayRow.startColumn, m_contentY - qreal(row) * m_metrics.lineHeight()};
}

// The content got a new size because rows moved, not because the user scrolled: where the view was
// clamped to the new size says nothing about where it should be, the anchor does.
void CodeEditor::updateContentSizeKeepingAnchor() {
  m_reanchoring = true;
  updateContentSize();
  m_reanchoring = false;
  restoreAnchor();
}

void CodeEditor::restoreAnchor() {
  const qsizetype row = m_map.rowForPosition({m_anchor.line, m_anchor.column});
  m_reanchoring = true;
  setContentY(qreal(row) * m_metrics.lineHeight() + m_anchor.offset);
  m_reanchoring = false;
}

void CodeEditor::setTheme(qce::Theme *theme) {
  if (!theme)
    theme = m_ownedTheme;
  if (theme == m_theme)
    return;
  disconnect(m_theme, &qce::Theme::changed, this, &CodeEditor::onThemeChanged);
  m_theme = theme;
  connect(m_theme, &qce::Theme::changed, this, &CodeEditor::onThemeChanged);
  emit themeChanged();
  onThemeChanged();
}

void CodeEditor::setHighlighter(qce::Highlighter *highlighter) {
  if (!highlighter)
    highlighter = m_nullHighlighter;
  if (highlighter == m_highlighter)
    return;
  disconnect(m_highlighter, &qce::Highlighter::invalidated, this, &CodeEditor::onHighlightInvalidated);
  disconnect(m_highlighter, &QObject::destroyed, this, nullptr);
  m_highlighter->detach();
  m_highlighter = highlighter;
  connect(m_highlighter, &qce::Highlighter::invalidated, this, &CodeEditor::onHighlightInvalidated);
  if (m_highlighter != m_nullHighlighter)
    connect(m_highlighter, &QObject::destroyed, this, [this] { setHighlighter(nullptr); });
  m_highlighter->attach(&m_document);
  onHighlightInvalidated(qce::Highlighter::AllLines, qce::Highlighter::AllLines);
  emit highlighterChanged();
}

QQmlListProperty<qce::Highlighter> CodeEditor::overlays() {
  return QQmlListProperty<qce::Highlighter>(
    this, nullptr,
    [](QQmlListProperty<qce::Highlighter> *list, qce::Highlighter *overlay) {
      static_cast<CodeEditor *>(list->object)->addOverlay(overlay);
    },
    [](QQmlListProperty<qce::Highlighter> *list) { return static_cast<CodeEditor *>(list->object)->m_overlays.size(); },
    [](QQmlListProperty<qce::Highlighter> *list, qsizetype index) {
      return static_cast<CodeEditor *>(list->object)->m_overlays.at(index);
    },
    [](QQmlListProperty<qce::Highlighter> *list) {
      auto *self = static_cast<CodeEditor *>(list->object);
      const QList<qce::Highlighter *> overlays = self->m_overlays;
      for (qce::Highlighter *overlay : overlays)
        self->removeOverlay(overlay);
    }
  );
}

void CodeEditor::addOverlay(qce::Highlighter *overlay) {
  if (!overlay || overlay == m_highlighter || m_overlays.contains(overlay))
    return;
  m_overlays.append(overlay);
  connect(overlay, &qce::Highlighter::invalidated, this, &CodeEditor::onHighlightInvalidated);
  connect(overlay, &QObject::destroyed, this, [this, overlay] {
    if (m_overlays.removeOne(overlay))
      invalidateLayouts();
  });
  overlay->attach(&m_document);
  invalidateLayouts();
}

void CodeEditor::removeOverlay(qce::Highlighter *overlay) {
  if (!m_overlays.removeOne(overlay))
    return;
  disconnect(overlay, nullptr, this, nullptr);
  overlay->detach();
  invalidateLayouts();
}

void CodeEditor::onHighlightInvalidated(qsizetype firstLine, qsizetype lastLine) {
  if (firstLine == qce::Highlighter::AllLines) {
    invalidateLayouts();
    return;
  }
  m_layouts.invalidate(firstLine, lastLine - firstLine + 1, lastLine - firstLine + 1);
  invalidatePlan();
}

void CodeEditor::invalidatePlan() {
  m_planDirty = true;
  polish();
}

void CodeEditor::invalidateLayouts() {
  m_layouts.clear();
  m_maxLineWidth = 0;
  invalidatePlan();
}

qsizetype CodeEditor::cursorPosition() const { return m_selections.primary().head; }
qsizetype CodeEditor::cursorLine() const { return m_document.rope().positionAt(cursorPosition()).line; }
qsizetype CodeEditor::cursorColumn() const { return m_document.rope().positionAt(cursorPosition()).column; }
qsizetype CodeEditor::selectionStart() const { return m_selections.primary().start(); }
qsizetype CodeEditor::selectionEnd() const { return m_selections.primary().end(); }

void CodeEditor::setCursorPosition(qsizetype offset) { select(offset, offset); }

void CodeEditor::select(qsizetype anchor, qsizetype head) {
  m_document.breakUndoCoalescing();
  m_selections.setSingle(anchor, head);
}

void CodeEditor::addSelection(qsizetype anchor, qsizetype head) {
  m_document.breakUndoCoalescing();
  m_selections.add({anchor, head});
}

bool CodeEditor::addCursorAbove() {
  qce::EditContext ctx = editContext();
  const bool done = qce::commands::addCursorVertical(ctx, true);
  afterCommand();
  return done;
}

bool CodeEditor::addCursorBelow() {
  qce::EditContext ctx = editContext();
  const bool done = qce::commands::addCursorVertical(ctx, false);
  afterCommand();
  return done;
}

bool CodeEditor::addNextOccurrence() {
  qce::EditContext ctx = editContext();
  const bool done = qce::commands::addNextOccurrence(ctx);
  afterCommand();
  return done;
}

bool CodeEditor::selectAllOccurrences() {
  qce::EditContext ctx = editContext();
  const bool done = qce::commands::selectAllOccurrences(ctx);
  afterCommand();
  return done;
}

bool CodeEditor::collapseSelections() {
  qce::EditContext ctx = editContext();
  const bool done = qce::commands::collapseSelections(ctx);
  afterCommand();
  return done;
}

qce::EditContext CodeEditor::editContext() {
  qce::EditorSettings settings;
  settings.insertSpaces = m_insertSpaces;
  settings.indentWidth = m_indentWidth;
  settings.tabWidth = m_metrics.tabWidth();
  settings.readOnly = m_readOnly;
  settings.skipFolds = m_foldPolicy == SkipFolds;
  settings.autoClose = m_autoClose;
  settings.autoClosePairs = m_autoClosePairs;
  return {m_document, m_selections, settings, &m_map, m_cursorLayout.get()};
}

void CodeEditor::setAutoClose(bool enable) {
  if (enable == m_autoClose)
    return;
  m_autoClose = enable;
  emit autoCloseChanged();
}

QStringList CodeEditor::autoClosePairs() const {
  QStringList list;
  for (const auto &pair : m_autoClosePairs)
    list.append(QString(QChar(pair.first)) + QChar(pair.second));
  return list;
}

void CodeEditor::setAutoClosePairs(const QStringList &pairs) {
  QList<std::pair<char16_t, char16_t>> parsed;
  for (const QString &pair : pairs)
    if (pair.size() == 2 && !pair[0].isSurrogate() && !pair[1].isSurrogate())
      parsed.append({pair[0].unicode(), pair[1].unicode()});
  if (parsed == m_autoClosePairs)
    return;
  m_autoClosePairs = parsed;
  m_bracketCache.clear();
  m_activeBlockCache.reset();
  invalidatePlan();
  emit autoClosePairsChanged();
}

void CodeEditor::setMatchBrackets(bool enable) {
  if (enable == m_matchBrackets)
    return;
  m_matchBrackets = enable;
  invalidatePlan();
  emit matchBracketsChanged();
}

void CodeEditor::setShowIndentGuides(bool show) {
  if (show == m_showIndentGuides)
    return;
  m_showIndentGuides = show;
  invalidatePlan();
  emit showIndentGuidesChanged();
}

void CodeEditor::setDetectIndentation(bool detect) {
  if (detect == m_detectIndentation)
    return;
  m_detectIndentation = detect;
  emit detectIndentationChanged();
  applyDetectedIndentation();
}

void CodeEditor::applyDetectedIndentation() {
  if (!m_detectIndentation)
    return;
  if (const auto guess = qce::detectIndentation(m_document.rope())) {
    setInsertSpaces(guess->insertSpaces);
    if (guess->indentWidth > 0)
      setIndentWidth(guess->indentWidth);
  }
}

void CodeEditor::setInsertSpaces(bool spaces) {
  if (spaces == m_insertSpaces)
    return;
  m_insertSpaces = spaces;
  invalidatePlan();
  emit insertSpacesChanged();
}

void CodeEditor::setIndentWidth(int columns) {
  columns = qBound(1, columns, 32);
  if (columns == m_indentWidth)
    return;
  m_indentWidth = columns;
  invalidatePlan();
  emit indentWidthChanged();
}

void CodeEditor::setUndoLimit(int steps) {
  steps = qMax(0, steps);
  if (steps == m_undoLimit)
    return;
  m_undoLimit = steps;
  m_document.undoStack().setLimit(steps);
  updateUndoState();
  emit undoLimitChanged();
}

void CodeEditor::setReadOnly(bool readOnly) {
  if (readOnly == m_readOnly)
    return;
  m_readOnly = readOnly;
  emit readOnlyChanged();
}

void CodeEditor::setInputHandler(qce::InputHandler *handler) {
  if (!handler)
    handler = &m_defaultHandler;
  if (handler == m_handler)
    return;
  qce::EditContext ctx = editContext();
  m_handler->reset();
  m_handler->deactivate(ctx, *m_host);
  m_handler = handler;
  m_handler->activate(ctx, *m_host);
  emit vimModeChanged();
  QGuiApplication::inputMethod()->update(Qt::ImEnabled | Qt::ImQueryInput);
  restartBlink();
  invalidatePlan();
  update();
}

bool CodeEditor::sendVimKeys(const QString &keys) {
  if (!vimMode())
    return false;
  qce::EditContext ctx = editContext();
  const bool handled = m_vim->feed(keys, ctx, *m_host);
  afterCommand();
  return handled;
}

void CodeEditor::setVimMode(bool enable) {
  if (enable == vimMode())
    return;
  setInputHandler(enable ? static_cast<qce::InputHandler *>(m_vim) : nullptr);
}

void CodeEditor::setSearchHighlight(const QRegularExpression &pattern) {
  if (pattern.pattern() == m_searchHighlight.pattern() && pattern.patternOptions() == m_searchHighlight.patternOptions())
    return;
  m_searchHighlight = pattern;
  invalidatePlan();
  update();
}

void CodeEditor::updateUndoState() {
  if (const bool can = m_document.canUndo(); can != m_canUndo) {
    m_canUndo = can;
    emit canUndoChanged();
  }
  if (const bool can = m_document.canRedo(); can != m_canRedo) {
    m_canRedo = can;
    emit canRedoChanged();
  }
}

// Housekeeping after anything the user did to the text or selections.
void CodeEditor::afterCommand() {
  updateUndoState();
  ensureCursorVisible();
}

void CodeEditor::keyPressEvent(QKeyEvent *event) {
  hidePopup();
  qce::EditContext ctx = editContext();
  if (m_handler->keyPress(event, ctx, *m_host)) {
    event->accept();
    afterCommand();
    return;
  }
  QQuickItem::keyPressEvent(event);
}

QPair<qsizetype, qsizetype> CodeEditor::unitRangeAt(qsizetype offset, DragUnit unit) const {
  const qce::Rope &rope = m_document.rope();
  if (unit == DragUnit::Word)
    return qce::TextBoundaries(rope).wordRangeAt(offset);
  if (unit == DragUnit::Line) {
    const qsizetype line = rope.lineAt(offset);
    // The line break goes with the line, so a triple-click selects what Delete would remove.
    // A folded line takes the lines folded into it along.
    const qsizetype next = m_map.folds().nextVisibleLine(line);
    return {rope.lineStart(line), next < rope.lineCount() ? rope.lineStart(next) : rope.length()};
  }
  return {offset, offset};
}

// A press in the gutter: tells the column's host, and in a column that selects lines starts a drag by
// whole lines (shift extends the selection from where it began).
void CodeEditor::handleGutterPress(QMouseEvent *event, bool doubleClick) {
  forceActiveFocus(Qt::MouseFocusReason);
  // The second press of a double click arrives as a press and again as a double-click event; it is
  // one click.
  if (doubleClick && event->timestamp() == m_lastGutterPress) {
    event->accept();
    return;
  }
  m_lastGutterPress = event->timestamp();
  qce::GutterColumn *column = columnAt(event->position().x());
  if (!column) {
    event->accept();
    return;
  }
  const qsizetype row = qBound<qsizetype>(
    0, qsizetype(std::floor((event->position().y() + m_contentY) / m_metrics.lineHeight())), m_map.rowCount() - 1
  );
  const qsizetype line = m_map.rowAt(row).line;
  emit column->clicked(line, int(event->button()), int(event->modifiers()));
  if (event->button() == Qt::LeftButton && column->selectsLines()) {
    const qce::Rope &rope = m_document.rope();
    m_document.breakUndoCoalescing();
    m_clickCount = 0; // a click in the text right after this is a first click
    m_dragging = true;
    m_dragUnit = DragUnit::Line;
    m_dragPos = event->position();
    if (event->modifiers() & Qt::ShiftModifier) {
      m_dragInitial = unitRangeAt(m_selections.primary().anchor, DragUnit::Line);
      m_dragAnchor = m_dragInitial.first;
      updateDrag();
    } else {
      m_dragInitial = unitRangeAt(rope.lineStart(line), DragUnit::Line);
      m_dragAnchor = m_dragInitial.first;
      m_selections.setSingle(m_dragInitial.first, m_dragInitial.second);
    }
  }
  event->accept();
}

void CodeEditor::hoverMoveEvent(QHoverEvent *event) { updateHover(event->position()); }

void CodeEditor::hoverLeaveEvent(QHoverEvent *event) {
  QQuickItem::hoverLeaveEvent(event);
  updateHover(QPointF(-1, -1));
}

// The pointer cursor follows what is under the pointer, and the gutter column under it hears which
// line that is.
void CodeEditor::updateHover(const QPointF &pos) {
  const bool inGutter = pos.x() >= 0 && pos.x() < m_gutterWidth;
  qce::GutterColumn *column = inGutter ? columnAt(pos.x()) : nullptr;
  qsizetype line = -1;
  bool onChip = false;
  if (column || (!inGutter && pos.x() >= 0 && !m_chipHits.isEmpty())) {
    const qsizetype row = qsizetype(std::floor((pos.y() + m_contentY) / m_metrics.lineHeight()));
    if (row >= 0 && row < m_map.rowCount()) {
      if (column) {
        line = m_map.rowAt(row).line;
      } else {
        const qreal x = pos.x() - m_gutterWidth + m_contentX;
        for (const ChipHit &chip : std::as_const(m_chipHits))
          if (chip.row == row && x >= chip.x0 && x < chip.x1)
            onChip = true;
      }
    }
  }
  if (column != m_hoverColumn || line != m_hoverLine) {
    qce::GutterColumn *previous = m_hoverColumn;
    m_hoverColumn = column;
    m_hoverLine = line;
    if (previous && previous != column)
      previous->hoverLine(-1);
    if (column)
      column->hoverLine(line);
  }
  const Qt::CursorShape shape = onChip ? Qt::PointingHandCursor : inGutter ? Qt::ArrowCursor : Qt::IBeamCursor;
  if (shape != cursor().shape())
    setCursor(shape);
  m_cursorInGutter = inGutter;
  updatePopupHover(pos);
}

void CodeEditor::handlePress(QMouseEvent *event, bool doubleClick) {
  hidePopup();
  if (event->position().x() < m_gutterWidth) {
    handleGutterPress(event, doubleClick);
    return;
  }
  forceActiveFocus(Qt::MouseFocusReason);
  if (event->button() == Qt::LeftButton && !m_chipHits.isEmpty()) {
    // A press on a fold's placeholder opens it.
    const qsizetype row = qsizetype(std::floor((event->position().y() + m_contentY) / m_metrics.lineHeight()));
    const qreal x = event->position().x() - m_gutterWidth + m_contentX;
    for (const ChipHit &chip : std::as_const(m_chipHits)) {
      if (chip.row == row && x >= chip.x0 && x < chip.x1) {
        const qsizetype line = chip.line;
        m_map.unfold(line);
        event->accept();
        return;
      }
    }
  }
  const qsizetype offset = positionAt(event->position().x(), event->position().y());
  if (event->button() == Qt::MiddleButton) {
    // Middle click pastes the selection clipboard where it was clicked (X11/Wayland convention).
    if (QGuiApplication::clipboard()->supportsSelection() && !m_readOnly) {
      m_selections.setSingle(offset);
      pasteFrom(QClipboard::Selection);
    }
    event->accept();
    return;
  }
  if (event->button() != Qt::LeftButton) {
    event->ignore();
    return;
  }

  // Alt (or Shift+Alt, which window managers leave alone) and drag: column selection.
  if ((event->modifiers() & Qt::AltModifier) && !(event->modifiers() & Qt::ControlModifier)) {
    m_document.breakUndoCoalescing();
    m_clickCount = 0;
    m_dragging = true;
    m_dragUnit = DragUnit::Box;
    m_dragPos = event->position();
    m_boxAnchorRow = qBound<qsizetype>(
      0, qsizetype(std::floor((event->position().y() + m_contentY) / m_metrics.lineHeight())), m_map.rowCount() - 1
    );
    m_boxAnchorX = qMax<qreal>(0, event->position().x() - m_gutterWidth + m_contentX);
    updateDrag();
    event->accept();
    return;
  }

  // Ctrl+click adds a cursor (and dragging makes it a selection); on an existing one it removes it.
  const Qt::KeyboardModifiers mods = event->modifiers();
  if ((mods & Qt::ControlModifier) && !(mods & (Qt::AltModifier | Qt::ShiftModifier))) {
    m_document.breakUndoCoalescing();
    m_clickCount = 0;
    const int existing = m_selections.indexAt(offset);
    if (existing >= 0 && m_selections.count() > 1) {
      qce::SelectionList list = m_selections.selections();
      list.removeAt(existing);
      m_selections.set(list, qMin(existing, int(list.size()) - 1));
    } else {
      m_dragBase = m_selections.selections();
      m_dragUnit = DragUnit::Add;
      m_dragAnchor = offset;
      m_dragging = true;
      m_dragPos = event->position();
      m_selections.add({offset, offset});
    }
    event->accept();
    return;
  }

  // Repeated clicks close together in time and space select a word, then a line.
  const QStyleHints *hints = QGuiApplication::styleHints();
  const bool repeated = m_clickCount > 0 && event->timestamp() - m_lastClickTime <= ulong(hints->mouseDoubleClickInterval()) &&
                        (event->position() - m_lastClickPos).manhattanLength() <= hints->mouseDoubleClickDistance();
  // Some platforms deliver the second press of a double click as a press followed by a
  // double-click event with the same timestamp; that is one click, counted once.
  const bool sameClick = doubleClick && m_clickCount > 0 && event->timestamp() == m_lastClickTime;
  if (!sameClick)
    m_clickCount = repeated ? qMin(m_clickCount + 1, 3) : 1;
  if (doubleClick)
    m_clickCount = qMax(m_clickCount, 2);
  m_lastClickTime = event->timestamp();
  m_lastClickPos = event->position();

  m_document.breakUndoCoalescing();
  m_dragging = true;
  m_dragUnit = m_clickCount == 1 ? DragUnit::Char : m_clickCount == 2 ? DragUnit::Word : DragUnit::Line;
  if (m_dragUnit == DragUnit::Char) {
    if (event->modifiers() & Qt::ShiftModifier) {
      m_dragAnchor = m_selections.primary().anchor; // extends the selection from where it began
    } else {
      m_dragAnchor = offset;
    }
    m_dragInitial = {m_dragAnchor, m_dragAnchor};
    m_selections.setSingle(m_dragAnchor, offset);
  } else {
    m_dragInitial = unitRangeAt(offset, m_dragUnit);
    m_dragAnchor = m_dragInitial.first;
    m_selections.setSingle(m_dragInitial.first, m_dragInitial.second);
  }
  m_dragPos = event->position();
  event->accept();
}

void CodeEditor::mousePressEvent(QMouseEvent *event) { handlePress(event, false); }
void CodeEditor::mouseDoubleClickEvent(QMouseEvent *event) { handlePress(event, true); }

// Extends the drag selection to the pointer. Word and line drags grow by whole units on whichever
// side of the first unit the pointer is.
void CodeEditor::updateDrag() {
  const qsizetype offset = positionAt(m_dragPos.x(), m_dragPos.y());
  if (m_dragUnit == DragUnit::Box) {
    const qsizetype row = qBound<qsizetype>(
      0, qsizetype(std::floor((m_dragPos.y() + m_contentY) / m_metrics.lineHeight())), m_map.rowCount() - 1
    );
    qce::EditContext ctx = editContext();
    qce::commands::boxSelect(
      ctx, m_boxAnchorRow, m_boxAnchorX, row, qMax<qreal>(0, m_dragPos.x() - m_gutterWidth + m_contentX)
    );
  } else if (m_dragUnit == DragUnit::Add) {
    qce::SelectionList list = m_dragBase;
    list.append({m_dragAnchor, offset});
    m_selections.set(list, int(list.size()) - 1);
  } else if (m_dragUnit == DragUnit::Char) {
    m_selections.setSingle(m_dragAnchor, offset);
  } else {
    const auto unit = unitRangeAt(offset, m_dragUnit);
    if (offset < m_dragInitial.first)
      m_selections.setSingle(m_dragInitial.second, unit.first);
    else
      m_selections.setSingle(m_dragInitial.first, unit.second);
  }
  ensureCursorVisible();
}

void CodeEditor::mouseMoveEvent(QMouseEvent *event) {
  if (!m_dragging) {
    event->ignore();
    return;
  }
  m_dragPos = event->position();
  updateDrag();
  const bool outside = !boundingRect().contains(m_dragPos);
  if (outside && !m_autoScrollTimer.isActive())
    m_autoScrollTimer.start();
  else if (!outside)
    m_autoScrollTimer.stop();
  event->accept();
}

// While the pointer is held outside the item the view keeps scrolling, faster the farther out it is.
void CodeEditor::autoScrollDrag() {
  const qreal dx = m_dragPos.x() < 0 ? m_dragPos.x() : m_dragPos.x() > width() ? m_dragPos.x() - width() : 0;
  const qreal dy = m_dragPos.y() < 0 ? m_dragPos.y() : m_dragPos.y() > height() ? m_dragPos.y() - height() : 0;
  setContentX(m_contentX + dx * 0.5 + (dx > 0 ? 1 : dx < 0 ? -1 : 0));
  setContentY(m_contentY + dy * 0.5 + (dy > 0 ? 1 : dy < 0 ? -1 : 0));
  // Over the edge the pointer's row is the one at the edge; positionAt clamps beyond the content.
  updateDrag();
}

void CodeEditor::endDrag() {
  m_dragging = false;
  m_autoScrollTimer.stop();
}

void CodeEditor::mouseReleaseEvent(QMouseEvent *event) {
  if (m_dragging && event->button() == Qt::LeftButton) {
    endDrag();
    // Selecting text makes it the primary selection, ready for a middle click elsewhere.
    setClipboardFromSelections(QClipboard::Selection);
    event->accept();
    return;
  }
  event->ignore();
}

void CodeEditor::mouseUngrabEvent() { endDrag(); }

qreal CodeEditor::preeditCursorX(const qce::LineLayout &layout) const {
  const qce::Injection *preedit = layout.preedit();
  if (!preedit)
    return layout.indentX;
  const int inside = m_preeditCursor >= 0 ? qMin<int>(m_preeditCursor, preedit->length) : preedit->length;
  return layout.indentX + layout.layout->lineAt(0).cursorToX(preedit->start + inside);
}

void CodeEditor::clearPreedit() {
  if (!hasPreedit())
    return;
  const qce::Rope &rope = m_document.rope();
  const qsizetype line = rope.lineAt(m_document.anchors().offset(m_preeditAnchor));
  m_document.anchors().remove(m_preeditAnchor);
  m_preeditAnchor = qce::InvalidAnchor;
  m_preedit.clear();
  m_preeditFormats.clear();
  m_preeditCursor = -1;
  m_layouts.invalidate(line, 1, 1);
  invalidatePlan();
}

void CodeEditor::inputMethodEvent(QInputMethodEvent *event) {
  if (m_readOnly || m_document.isLoading() || !m_handler->acceptsTextInput()) {
    event->ignore();
    return;
  }
  m_inImeEvent = true;
  clearPreedit();

  const QString commit = event->commitString();
  if (!commit.isEmpty() || event->replacementLength() > 0) {
    // The replaced text is given relative to the cursor, in characters.
    if (event->replacementLength() > 0 || event->replacementStart() != 0) {
      const qsizetype length = m_document.length();
      const qsizetype from = qBound<qsizetype>(0, cursorPosition() + event->replacementStart(), length);
      const qsizetype to = qBound<qsizetype>(from, from + event->replacementLength(), length);
      m_selections.setSingle(from, to);
    }
    qce::EditContext ctx = editContext();
    if (event->replacementLength() == 0 && event->replacementStart() == 0)
      m_handler->commitText(commit, ctx, *m_host);
    else
      qce::commands::insertText(ctx, commit, qce::EditKind::Typing);
  }

  const QString preedit = event->preeditString();
  if (!preedit.isEmpty()) {
    m_preedit = preedit;
    m_preeditAnchor = m_document.anchors().create(cursorPosition(), qce::Gravity::Left);
    for (const QInputMethodEvent::Attribute &attribute : event->attributes()) {
      if (attribute.type == QInputMethodEvent::Cursor) {
        m_preeditCursor = attribute.length > 0 ? attribute.start : -1;
      } else if (attribute.type == QInputMethodEvent::TextFormat) {
        const QTextFormat format = qvariant_cast<QTextFormat>(attribute.value);
        if (format.isCharFormat() && attribute.length > 0)
          m_preeditFormats.append({attribute.start, attribute.length, format.toCharFormat()});
      }
    }
    if (m_preeditFormats.isEmpty()) { // no styling from the platform: underline it
      QTextCharFormat underline;
      underline.setFontUnderline(true);
      m_preeditFormats.append({0, int(preedit.size()), underline});
    }
    std::sort(m_preeditFormats.begin(), m_preeditFormats.end(), [](const auto &a, const auto &b) { return a.start < b.start; });
    m_layouts.invalidate(m_document.rope().lineAt(cursorPosition()), 1, 1);
    invalidatePlan();
  }
  m_inImeEvent = false;
  event->accept();
  afterCommand();
  QGuiApplication::inputMethod()->update(Qt::ImQueryInput);
}

QVariant CodeEditor::inputMethodQuery(Qt::InputMethodQuery query) const {
  constexpr qsizetype kContext = 4096; // how much text around the cursor an input method gets
  const qce::Rope &rope = m_document.rope();
  const qsizetype head = cursorPosition();
  const qce::TextPosition pos = rope.positionAt(head);
  const qsizetype lineStart = rope.lineStart(pos.line);
  auto lineText = [&] { return rope.toString(lineStart, rope.lineEnd(pos.line)); };
  switch (query) {
  case Qt::ImEnabled:
    return !m_readOnly && m_handler->acceptsTextInput();
  case Qt::ImReadOnly:
    return m_readOnly;
  case Qt::ImHints:
    return int(Qt::ImhNoAutoUppercase | Qt::ImhNoPredictiveText | Qt::ImhMultiLine);
  case Qt::ImFont:
    return m_font;
  case Qt::ImCursorRectangle: {
    // At the cursor inside the composition when there is one, so candidates sit under what is typed.
    QRectF rect = const_cast<CodeEditor *>(this)->rectForPosition(head);
    if (hasPreedit())
      if (const auto layout = const_cast<CodeEditor *>(this)->layoutForRow(rowOfPosition(pos), m_document.snapshot());
          layout->preedit())
        rect.moveLeft(preeditCursorX(*layout) - m_contentX + m_gutterWidth);
    return rect;
  }
  case Qt::ImCursorPosition:
    return int(pos.column);
  case Qt::ImSurroundingText:
    return lineText();
  case Qt::ImAnchorPosition: {
    const qce::Selection s = m_selections.primary();
    const qsizetype anchor = qBound(lineStart, s.anchor, rope.lineEnd(pos.line));
    return int(anchor - lineStart);
  }
  case Qt::ImAbsolutePosition:
    return int(qMin<qsizetype>(head, std::numeric_limits<int>::max()));
  case Qt::ImTextBeforeCursor: {
    const qsizetype count = qMin<qsizetype>(kContext, pos.column);
    return rope.toString(head - count, head);
  }
  case Qt::ImTextAfterCursor: {
    const qsizetype count = qMin<qsizetype>(kContext, rope.lineEnd(pos.line) - head);
    return rope.toString(head, head + count);
  }
  case Qt::ImCurrentSelection: {
    const qce::Selection s = m_selections.primary();
    return rope.toString(s.start(), qMin(s.end(), s.start() + kContext));
  }
  default:
    return QQuickItem::inputMethodQuery(query);
  }
}

void CodeEditor::undo() {
  qce::EditContext ctx = editContext();
  qce::commands::undo(ctx);
  afterCommand();
}

void CodeEditor::redo() {
  qce::EditContext ctx = editContext();
  qce::commands::redo(ctx);
  afterCommand();
}

void CodeEditor::selectAll() {
  qce::EditContext ctx = editContext();
  qce::commands::selectAll(ctx);
  afterCommand();
}

void CodeEditor::insert(const QString &text) {
  qce::EditContext ctx = editContext();
  qce::commands::insertText(ctx, text, qce::EditKind::Other);
  afterCommand();
}

namespace {

// Above this many UTF-16 units clipboard text is built or consumed off the GUI thread.
constexpr qsizetype kLargeClipboard = 1 << 20;
// The per-cursor pieces of a multi-selection copy, as a JSON array of strings.
const QString kPiecesMime = QStringLiteral("application/x-qce-pieces");

QString joinSlices(const QList<qce::Rope> &slices) {
  QString out;
  qsizetype total = std::max<qsizetype>(0, slices.size() - 1);
  for (const qce::Rope &r : slices)
    total += r.length();
  out.reserve(total);
  for (qsizetype i = 0; i < slices.size(); ++i) {
    if (i > 0)
      out += u'\n';
    out += slices[i].toString();
  }
  return out;
}

} // namespace

// The text of every non-empty selection, joined by line breaks. Ropes are persistent, so taking the
// slices is cheap whatever their size; turning them into a QString (the expensive part of copying
// a huge selection) happens on a worker.
void CodeEditor::setClipboardFromSelections(QClipboard::Mode mode) {
  const qce::Rope &rope = m_document.rope();
  // With several selections every one contributes a piece, empty ones included, so that pasting
  // into the same number of cursors hands each its own text.
  const bool multi = m_selections.count() > 1;
  QList<qce::Rope> slices;
  qsizetype total = 0;
  bool anyText = false;
  for (int i = 0; i < m_selections.count(); ++i) {
    const qce::Selection s = m_selections.at(i);
    if (s.isEmpty() && !multi)
      continue;
    anyText |= !s.isEmpty();
    slices.append(rope.slice(s.start(), s.end()));
    total += s.end() - s.start();
  }
  if (slices.isEmpty() || !anyText)
    return;
  QClipboard *clipboard = QGuiApplication::clipboard();
  if (mode == QClipboard::Selection && !clipboard->supportsSelection())
    return;
  if (total < kLargeClipboard) {
    auto *data = new QMimeData;
    data->setText(joinSlices(slices));
    if (multi) {
      QJsonArray pieces;
      for (const qce::Rope &r : std::as_const(slices))
        pieces.append(r.toString());
      data->setData(kPiecesMime, QJsonDocument(pieces).toJson(QJsonDocument::Compact));
    }
    clipboard->setMimeData(data, mode);
    return;
  }
  auto *watcher = new QFutureWatcher<QString>(this);
  connect(watcher, &QFutureWatcher<QString>::finished, this, [watcher, mode] {
    QGuiApplication::clipboard()->setText(watcher->result(), mode);
    watcher->deleteLater();
  });
  watcher->setFuture(QtConcurrent::run([slices] { return joinSlices(slices); }));
}

void CodeEditor::copy() { setClipboardFromSelections(QClipboard::Clipboard); }

void CodeEditor::cut() {
  if (m_readOnly)
    return;
  copy();
  qce::EditContext ctx = editContext();
  qce::commands::deleteSelection(ctx);
  afterCommand();
}

void CodeEditor::paste() { pasteFrom(QClipboard::Clipboard); }

void CodeEditor::pasteFrom(QClipboard::Mode mode) {
  if (m_readOnly || m_document.isLoading())
    return;
  const QString text = QGuiApplication::clipboard()->text(mode);
  if (text.isEmpty())
    return;
  if (text.size() < kLargeClipboard || m_selections.count() != 1) {
    qce::EditContext ctx = editContext();
    QStringList pieces;
    if (m_selections.count() > 1)
      if (const QMimeData *data = QGuiApplication::clipboard()->mimeData(mode); data && data->hasFormat(kPiecesMime))
        for (const QJsonValue &v : QJsonDocument::fromJson(data->data(kPiecesMime)).array())
          pieces.append(v.toString());
    qce::commands::paste(ctx, text, pieces);
    afterCommand();
    return;
  }

  // A big paste: build the rope on a worker. The target range is held by anchors, so edits made in
  // the meantime move it instead of invalidating it.
  const qce::Selection target = m_selections.primary();
  qce::AnchorSet &anchors = m_document.anchors();
  const qce::AnchorId start = anchors.create(target.start(), qce::Gravity::Left);
  const qce::AnchorId end = anchors.create(target.end(), qce::Gravity::Right);
  ++m_pendingPastes;
  auto *watcher = new QFutureWatcher<qce::Rope>(this);
  connect(watcher, &QFutureWatcher<qce::Rope>::finished, this, [this, watcher, start, end] {
    qce::AnchorSet &anchors = m_document.anchors();
    const qsizetype from = anchors.offset(start), to = anchors.offset(end);
    anchors.remove(start);
    anchors.remove(end);
    --m_pendingPastes;
    if (!m_readOnly && !m_document.isLoading()) {
      qce::EditContext ctx = editContext();
      qce::commands::applyReplacements(ctx, {{from, to, watcher->result()}}, qce::EditKind::Other);
      afterCommand();
    }
    watcher->deleteLater();
  });
  watcher->setFuture(QtConcurrent::run([text] { return qce::Rope::fromString(text); }));
}

void CodeEditor::ensureCursorVisible() {
  const qce::TextSnapshot snapshot = m_document.snapshot();
  const qce::TextPosition pos = m_map.visiblePosition(snapshot.rope().positionAt(cursorPosition()));
  const auto layout = layoutForRow(rowOfPosition(pos), snapshot);
  if (layout->indentX + layout->width > m_maxLineWidth) {
    m_maxLineWidth = layout->indentX + layout->width;
    updateContentSize();
  }
  const qreal lineHeight = m_metrics.lineHeight();
  const qreal top = qreal(m_map.rowForPosition(pos)) * lineHeight;
  if (top < m_contentY)
    setContentY(top);
  else if (top + lineHeight > m_contentY + height())
    setContentY(top + lineHeight - height());
  const qreal x = xForColumn(*layout, pos.column);
  const qreal margin = 2 * m_metrics.cellAdvance();
  if (x - margin < m_contentX)
    setContentX(x - margin);
  else if (x + margin + m_metrics.cellAdvance() > m_contentX + textViewportWidth())
    setContentX(x + margin + m_metrics.cellAdvance() - textViewportWidth());
}

// Selections moved: by a command, by select(), or because an edit shifted their anchors. The
// signal goes out only when the primary selection or the set really changed.
void CodeEditor::onSelectionsChanged() {
  const qce::Selection now = m_selections.primary();
  const bool same = now == m_lastSelection && m_selections.count() == m_lastSelectionCount;
  m_lastSelection = now;
  m_lastSelectionCount = m_selections.count();
  if (hasPreedit() && !m_inImeEvent) {
    // The cursor moved under a composition (a click, say): the composition is over.
    clearPreedit();
    QGuiApplication::inputMethod()->reset();
  }
  invalidatePlan();
  revealCursor();
  if (same)
    return;
  emit selectionChanged();
  restartBlink();
  if (m_hasFocus)
    QGuiApplication::inputMethod()->update(Qt::ImCursorRectangle | Qt::ImCursorPosition | Qt::ImSurroundingText | Qt::ImAnchorPosition);
}

void CodeEditor::setCursorBlinkInterval(int ms) {
  ms = qMax(0, ms);
  if (ms == cursorBlinkInterval())
    return;
  m_blinkTimer.setInterval(ms);
  emit cursorBlinkIntervalChanged();
  restartBlink();
}

// Shows the cursor and restarts the blink phase, so it never vanishes right after it moved.
void CodeEditor::restartBlink() {
  if (!m_cursorVisible) {
    m_cursorVisible = true;
    emit cursorVisibleChanged();
  }
  m_blinkPhases = 0;
  // Only a focused editor blinks (and shows) its cursor: an idle one must not keep rendering.
  if (m_blinkTimer.interval() > 0 && m_hasFocus)
    m_blinkTimer.start();
  else
    m_blinkTimer.stop();
}

void CodeEditor::focusInEvent(QFocusEvent *event) {
  QQuickItem::focusInEvent(event);
  m_hasFocus = true;
  restartBlink();
  update();
}

void CodeEditor::focusOutEvent(QFocusEvent *event) {
  QQuickItem::focusOutEvent(event);
  m_hasFocus = false;
  m_handler->reset();
  m_document.breakUndoCoalescing();
  endDrag();
  if (!popupHasFocus()) // focus moving into the popup (to select its text) must not close it
    hidePopup();
  restartBlink(); // stops the timer
  update();
}

void CodeEditor::setContentX(qreal x) {
  x = qBound<qreal>(0, x, qMax<qreal>(0, m_contentWidth - textViewportWidth()));
  if (x == m_contentX)
    return;
  m_contentX = x;
  hidePopup();
  emit contentXChanged();
  invalidatePlan(); // the current-line highlight spans the visible columns
}

void CodeEditor::setContentY(qreal y) {
  y = qBound<qreal>(0, y, qMax<qreal>(0, m_contentHeight - height()));
  if (y == m_contentY)
    return;
  m_contentY = y;
  hidePopup();
  if (!m_reanchoring)
    captureAnchor();
  emit contentYChanged();
  polish(); // cheap when the layout window still covers the viewport
}

void CodeEditor::updateContentSize() {
  const qreal height = qreal(m_map.rowCount()) * m_metrics.lineHeight();
  if (height != m_contentHeight) {
    m_contentHeight = height;
    emit contentHeightChanged();
  }
  // One cell of slack so a cursor at the end of the widest line is reachable. Wrapped text is as
  // wide as its rows, which are the item (or the column) wide.
  qreal width = m_maxLineWidth > 0 ? m_maxLineWidth + m_metrics.cellAdvance() : 0;
  if (m_map.wrapEnabled())
    width = m_map.wrapConfig().mode == qce::WrapMode::Viewport ? textViewportWidth() : m_map.wrapConfig().rowWidth() + m_metrics.cellAdvance();
  if (width != m_contentWidth) {
    m_contentWidth = width;
    emit contentWidthChanged();
  }
  setContentX(m_contentX);
  setContentY(m_contentY);
}

void CodeEditor::wheelEvent(QWheelEvent *event) {
  QPointF delta = event->pixelDelta().isNull()
                    ? QPointF(event->angleDelta()) * (m_metrics.lineHeight() * 3 / 120.0)
                    : QPointF(event->pixelDelta());
  if (event->modifiers() & Qt::ShiftModifier && delta.x() == 0)
    delta = QPointF(delta.y(), 0);
  const qreal beforeX = m_contentX, beforeY = m_contentY;
  setContentX(m_contentX - delta.x());
  setContentY(m_contentY - delta.y());
  event->setAccepted(m_contentX != beforeX || m_contentY != beforeY);
}

// `column` is a column of the buffer line. The layout holds one row of it, so the column is made
// relative to the row first; a row showing an input-method composition has the preedit text laid
// out inside it, so columns at or after it shift right.
qreal CodeEditor::xForColumn(const qce::LineLayout &layout, qsizetype column) const {
  column = qBound<qsizetype>(0, column - layout.startColumn, layout.text.size());
  if (layout.injections.isEmpty() && m_metrics.isSimple(layout.text))
    return layout.indentX + m_metrics.xForColumn(layout.text, column);
  return layout.indentX + layout.layout->lineAt(0).cursorToX(layout.layoutIndex(int(column)));
}

// A thin line across each tab's span on the baseline, for the rows being drawn. Capped so a
// minified file full of tabs can't make a frame expensive.
void CodeEditor::buildTabMarks() {
  constexpr qsizetype kMaxMarks = 4000;
  const qreal markY = qFloor(m_metrics.ascent() * 0.6);
  for (const qce::FramePlanRow &planRow : std::as_const(m_plan)) {
    const qce::LineLayout &layout = *planRow.layout;
    const QString &text = layout.text;
    if (!text.contains(u'\t'))
      continue;
    if (!layout.injections.isEmpty())
      continue; // tab marks are placed by column; injected text shifts them
    const bool simple = m_metrics.isSimple(text);
    qsizetype cell = 0;
    for (qsizetype i = 0; i < text.size(); ++i) {
      const bool tab = text[i] == u'\t';
      const qsizetype nextCell = cell + (tab ? m_metrics.tabWidth() - cell % m_metrics.tabWidth() : 1);
      if (tab) {
        const qreal x0 = layout.indentX + (simple ? cell * m_metrics.cellAdvance() : layout.layout->lineAt(0).cursorToX(int(i)));
        const qreal x1 =
          layout.indentX + (simple ? nextCell * m_metrics.cellAdvance() : layout.layout->lineAt(0).cursorToX(int(i) + 1));
        if (x1 - x0 > 4)
          m_markSpans.append({planRow.row, x0 + 2, x1 - 2, markY, 1});
        if (m_markSpans.size() >= kMaxMarks)
          return;
      }
      cell = nextCell;
    }
  }
}

// Cursor, selection and current-line spans for the rows in the plan. A selection covering the
// whole of a huge document still only produces spans for the rows being drawn.
void CodeEditor::buildOverlays() {
  m_currentLineSpans.clear();
  m_selectionSpans.clear();
  m_markSpans.clear();
  if (m_showWhitespace)
    buildTabMarks();
  m_cursorSpans.clear();
  buildFoldChips();
  const qce::Rope &rope = m_document.rope();
  auto planLayout = [&](qsizetype row) -> const qce::LineLayout * {
    return row >= m_planFirst && row <= m_planLast ? m_plan[row - m_planFirst].layout.get() : nullptr;
  };
  const qreal cell = m_metrics.cellAdvance();

  // Selections are sorted, so the ones that can show are found by binary search and the rest (there
  // may be tens of thousands) are never looked at. The range runs from the first plan row's line to
  // the start of the visible line after the last one, which takes in the text folded under it.
  if (m_planLast < m_planFirst || m_planFirst < 0)
    return;
  const qsizetype firstLine = m_map.rowAt(m_planFirst).line;
  const qsizetype afterLine = m_map.folds().nextVisibleLine(m_map.rowAt(m_planLast).line);
  const qsizetype lowOffset = rope.lineStart(firstLine);
  const qsizetype highOffset = afterLine < rope.lineCount() ? rope.lineStart(afterLine) : rope.length();
  const qreal viewLeft = m_contentX - cell, viewRight = m_contentX + textViewportWidth() + cell;
  const int primary = m_selections.primaryIndex();
  // The input handler picks the cursor's shape and the character it sits on (vim: a block).
  const qce::CursorShape shape = m_handler->cursorShape();
  const qce::EditContext handlerContext = editContext();
  for (int i = m_selections.lowerBound(lowOffset); i < m_selections.count(); ++i) {
    const qce::Selection sel = m_selections.at(i);
    if (sel.start() > highOffset)
      break;
    const qce::TextPosition head = m_map.visiblePosition(rope.positionAt(sel.head));
    const qsizetype cursorAt = m_handler->cursorOffset(i, sel, handlerContext);
    if (cursorAt >= 0) {
      const qce::TextPosition at = cursorAt == sel.head ? head : m_map.visiblePosition(rope.positionAt(cursorAt));
      const qsizetype cursorRow = m_map.rowForPosition(at);
      if (const qce::LineLayout *layout = planLayout(cursorRow)) {
        const qreal x = i == primary && layout->preedit() ? preeditCursorX(*layout) : xForColumn(*layout, at.column);
        if (x >= viewLeft && x <= viewRight) {
          if (shape == qce::CursorShape::Line) {
            m_cursorSpans.append({cursorRow, x, x + 2});
          } else {
            // Over the character: its width, or one cell where there is none (an empty line).
            qreal x1 = x + cell;
            if (cursorAt < rope.lineEnd(at.line)) {
              const qsizetype units = qce::TextBoundaries(rope).nextGrapheme(cursorAt) - cursorAt;
              x1 = qMax(x1 - cell + 1, xForColumn(*layout, at.column + units));
            }
            if (shape == qce::CursorShape::Underline)
              m_cursorSpans.append({cursorRow, x, x1, m_metrics.lineHeight() - 2, 2});
            else
              m_cursorSpans.append({cursorRow, x, x1});
          }
        }
      }
    }
    if (i == primary && sel.isEmpty()) {
      // The highlight covers every row of the cursor's line.
      const qsizetype first = m_map.firstRowOfLine(head.line);
      const qsizetype last = first + m_map.rowCountOfLine(head.line) - 1;
      for (qsizetype row = qMax(first, m_planFirst); row <= qMin(last, m_planLast); ++row)
        m_currentLineSpans.append({row, m_contentX, m_contentX + textViewportWidth()});
    }
    if (sel.isEmpty())
      continue;
    const qce::TextPosition start = m_map.visiblePosition(rope.positionAt(sel.start()));
    const qce::TextPosition end = m_map.visiblePosition(rope.positionAt(sel.end()));
    const qsizetype startRow = m_map.rowForPosition(start);
    const qsizetype endRow = m_map.rowForPosition(end);
    for (qsizetype row = qMax(startRow, m_planFirst); row <= qMin(endRow, m_planLast); ++row) {
      const qce::LineLayout *layout = planLayout(row);
      const qreal x0 = row == startRow ? xForColumn(*layout, start.column) : layout->indentX;
      // A row the selection continues past ends at its text; at the end of a line the break counts
      // as one more cell.
      const qreal x1 = row == endRow ? xForColumn(*layout, end.column)
                                     : layout->indentX + layout->width + (layout->endsLine ? cell : 0);
      if (x1 > x0 && x1 >= viewLeft && x0 <= viewRight)
        m_selectionSpans.append({row, x0, x1});
    }
  }
}

// The column of the buffer line nearest to x, for the row laid out in `layout`.
qsizetype CodeEditor::columnForX(const qce::LineLayout &layout, qreal x) const {
  x -= layout.indentX;
  if (layout.injections.isEmpty() && m_metrics.isSimple(layout.text))
    return layout.startColumn + m_metrics.columnForX(layout.text, x);
  const int index = layout.layout->lineAt(0).xToCursor(x, QTextLine::CursorBetweenCharacters);
  // Injected text (a composition, a hint) is not part of the text: hits on it land next to it, and
  // hits on virtual text after the row's end land at its end.
  return layout.startColumn + qMin<qsizetype>(layout.columnForLayoutIndex(index), layout.text.size());
}

qce::DisplayRow CodeEditor::rowOfPosition(qce::TextPosition position) const {
  return m_map.rowAt(m_map.rowForPosition(position));
}

qsizetype CodeEditor::positionAt(qreal x, qreal y) {
  const qsizetype row = qBound<qsizetype>(
    0, qsizetype(std::floor((y + m_contentY) / m_metrics.lineHeight())), m_map.rowCount() - 1
  );
  const qce::DisplayRow displayRow = m_map.rowAt(row);
  const qce::TextSnapshot snapshot = m_document.snapshot();
  const qce::Rope &rope = snapshot.rope();
  const auto layout = layoutForRow(displayRow, snapshot);
  const qsizetype column =
    qBound(displayRow.startColumn, columnForX(*layout, x - m_gutterWidth + m_contentX), displayRow.lastCursorColumn());
  return rope.snapToCodePoint(rope.offsetAt({displayRow.line, column}));
}

QRectF CodeEditor::rectForPosition(qsizetype offset) {
  const qce::TextSnapshot snapshot = m_document.snapshot();
  const qce::TextPosition position = m_map.visiblePosition(snapshot.rope().positionAt(offset));
  const qsizetype row = m_map.rowForPosition(position);
  const auto layout = layoutForRow(m_map.rowAt(row), snapshot);
  const qreal cursorX = xForColumn(*layout, position.column);
  return QRectF(
    cursorX - m_contentX + m_gutterWidth, qreal(row) * m_metrics.lineHeight() - m_contentY, m_metrics.cellAdvance(),
    m_metrics.lineHeight()
  );
}

// Folding ------------------------------------------------------------------------------------

void CodeEditor::setFoldProvider(qce::FoldProvider *provider) {
  if (!provider)
    provider = m_defaultFolds;
  if (provider == m_foldProvider)
    return;
  disconnect(m_foldProvider, &qce::FoldProvider::invalidated, this, &CodeEditor::invalidatePlan);
  disconnect(m_foldProvider, &QObject::destroyed, this, nullptr);
  m_foldProvider = provider;
  connect(provider, &qce::FoldProvider::invalidated, this, &CodeEditor::invalidatePlan);
  if (provider != m_defaultFolds)
    connect(provider, &QObject::destroyed, this, [this] { setFoldProvider(nullptr); });
  invalidatePlan();
  emit foldProviderChanged();
}

void CodeEditor::setFoldCursorPolicy(FoldCursorPolicy policy) {
  if (policy == m_foldPolicy)
    return;
  m_foldPolicy = policy;
  emit foldCursorPolicyChanged();
}

QList<qce::FoldRange> CodeEditor::foldRangesIn(qsizetype firstLine, qsizetype lastLine) {
  return m_foldProvider->foldRanges(m_document.snapshot(), firstLine, lastLine);
}

// Rows moved because lines were hidden or shown: keep the text at the top of the view where it was.
void CodeEditor::onFoldsChanged() {
  m_planDirty = true;
  updateContentSizeKeepingAnchor();
  polish();
}

// After a fold command: the cursor must not stay in text that just disappeared, and rows moved.
bool CodeEditor::foldChanged(bool changed) {
  if (!changed)
    return false;
  if (moveSelectionsOutOfFolds())
    ensureCursorVisible();
  return true;
}

// Selections with an end inside a folded line move to the end of the line that holds the fold.
bool CodeEditor::moveSelectionsOutOfFolds() {
  if (!m_map.folds().hasFolds())
    return false;
  const qce::Rope &rope = m_document.rope();
  qce::SelectionList list = m_selections.selections();
  bool moved = false;
  auto fix = [&](qsizetype &offset) {
    const qsizetype line = rope.lineAt(offset);
    if (m_map.folds().isHidden(line)) {
      offset = rope.lineEnd(m_map.folds().visibleHeaderOf(line));
      moved = true;
    }
  };
  for (qce::Selection &s : list) {
    fix(s.anchor);
    fix(s.head);
  }
  if (moved)
    m_selections.set(list, m_selections.primaryIndex());
  return moved;
}

// A cursor that ended up inside a fold (a click can't; a programmatic move, undo, or a key with the
// UnfoldOnEnter policy can) opens it.
void CodeEditor::revealCursor() {
  if (!m_map.folds().hasFolds())
    return;
  const qce::Rope &rope = m_document.rope();
  for (int i = 0; i < m_selections.count(); ++i) {
    const qce::Selection sel = m_selections.at(i);
    if (!sel.isEmpty())
      continue;
    const qsizetype line = rope.lineAt(sel.head);
    if (m_map.folds().isHidden(line))
      m_map.unfoldContaining(line);
  }
}

bool CodeEditor::fold(qsizetype line) {
  const auto range = m_foldProvider->rangeAt(m_document.snapshot(), line);
  return range && foldChanged(m_map.fold(range->startLine, range->endLine));
}

bool CodeEditor::unfold(qsizetype line) { return m_map.unfold(line); }

bool CodeEditor::toggleFold(qsizetype line) {
  return foldChanged(qce::folding::toggle(m_map, *m_foldProvider, m_document.snapshot(), line));
}

bool CodeEditor::isFolded(qsizetype line) const { return m_map.folds().isFolded(line); }

bool CodeEditor::foldAtCursor() {
  return foldChanged(qce::folding::foldAt(m_map, *m_foldProvider, m_document.snapshot(), cursorLine()));
}

bool CodeEditor::unfoldAtCursor() { return qce::folding::unfoldAt(m_map, cursorLine()); }

bool CodeEditor::foldAll() {
  return foldChanged(qce::folding::foldAll(m_map, *m_foldProvider, m_document.snapshot()));
}

bool CodeEditor::unfoldAll() { return qce::folding::unfoldAll(m_map); }

bool CodeEditor::foldToLevel(int level) {
  return foldChanged(qce::folding::foldToLevel(m_map, *m_foldProvider, m_document.snapshot(), level));
}

void CodeEditor::foldCommand(qce::FoldCommand command) {
  switch (command) {
  case qce::FoldCommand::FoldAtCursor: foldAtCursor(); break;
  case qce::FoldCommand::UnfoldAtCursor: unfoldAtCursor(); break;
  case qce::FoldCommand::FoldAll: foldAll(); break;
  case qce::FoldCommand::UnfoldAll: unfoldAll(); break;
  }
}

// A chip after the last row of every folded line in the plan: a rounded-looking pill with three dots.
void CodeEditor::buildFoldChips() {
  m_chipSpans.clear();
  m_chipDotSpans.clear();
  m_chipHits.clear();
  if (!m_map.folds().hasFolds())
    return;
  const qreal cell = m_metrics.cellAdvance(), lh = m_metrics.lineHeight();
  const qreal dot = qMax<qreal>(2, std::round(lh / 9));
  const qreal inset = std::round(lh / 6);
  for (const qce::FramePlanRow &planRow : std::as_const(m_plan)) {
    if (!planRow.display.isLast() || !m_map.folds().isFolded(planRow.display.line))
      continue;
    const qreal x0 = planRow.layout->indentX + planRow.layout->fullWidth + cell;
    const qreal x1 = x0 + 4 * cell;
    m_chipSpans.append({planRow.row, x0, x1, inset, lh - 2 * inset});
    const qreal dy = std::round((lh - dot) / 2) + 1;
    for (int i = 0; i < 3; ++i) {
      const qreal x = x0 + 2 * cell + (i - 1) * 1.1 * cell - dot / 2;
      m_chipDotSpans.append({planRow.row, std::round(x), std::round(x) + dot, dy, dot});
    }
    m_chipHits.append({planRow.row, x0, x1, planRow.display.line});
  }
}

// Popups -------------------------------------------------------------------------------------

void CodeEditor::setPopupDelegate(QQmlComponent *delegate) {
  if (delegate == m_popupDelegate)
    return;
  hidePopup();
  m_popupDelegate = delegate;
  emit popupDelegateChanged();
}

void CodeEditor::setDiagnosticPopups(bool enable) {
  if (enable == m_diagnosticPopups)
    return;
  m_diagnosticPopups = enable;
  if (!enable)
    hidePopup();
  emit diagnosticPopupsChanged();
}

void CodeEditor::setHoverDelay(int ms) {
  ms = qMax(0, ms);
  if (ms == m_hoverTimer.interval())
    return;
  m_hoverTimer.setInterval(ms);
  emit hoverDelayChanged();
}

QQmlComponent *CodeEditor::popupComponent() {
  if (m_popupDelegate)
    return m_popupDelegate;
  if (!m_defaultPopup) {
    QQmlEngine *engine = qmlEngine(this);
    if (!engine)
      return nullptr; // an editor made in C++ with no engine has no default popup
    m_defaultPopup = new QQmlComponent(engine, QUrl(u"qrc:/qt/qml/me/blq/qmlcodeeditor/DiagnosticPopup.qml"_s), this);
    if (m_defaultPopup->isError())
      qWarning() << m_defaultPopup->errors();
  }
  return m_defaultPopup;
}

QList<qce::Diagnostic> CodeEditor::diagnosticsFor(const HoverTarget &target) const {
  if (target.kind == HoverTarget::Text)
    return m_diagnostics.at(target.offset);
  if (target.kind == HoverTarget::Line) {
    const qce::Rope &rope = m_document.rope();
    QList<qce::Diagnostic> list = m_diagnostics.inRange(rope.lineStart(target.value), rope.lineEnd(target.value));
    std::stable_sort(list.begin(), list.end(), [](const auto &a, const auto &b) { return a.severity < b.severity; });
    return list;
  }
  return {};
}

// Where the popup hangs from, in item coordinates: the character cell, or for a whole line the left
// edge of its first row.
QRectF CodeEditor::anchorFor(const HoverTarget &target) {
  if (target.kind == HoverTarget::Text)
    return rectForPosition(target.offset);
  const qreal lh = m_metrics.lineHeight();
  const qsizetype row = m_map.firstRowOfLine(target.value);
  return QRectF(target.anchorX, qreal(row) * lh - m_contentY, 1, lh);
}

bool CodeEditor::showPopup(const HoverTarget &target) {
  hidePopup();
  const QList<qce::Diagnostic> diagnostics = diagnosticsFor(target);
  QQmlComponent *component = popupComponent();
  if (diagnostics.isEmpty() || !component || !window())
    return false;
  QVariantList list;
  for (const qce::Diagnostic &d : diagnostics)
    list.append(d.toLsp());
  QObject *object = component->createWithInitialProperties(
    {{u"diagnostics"_s, list}, {u"editor"_s, QVariant::fromValue(this)}}, qmlContext(this)
  );
  auto *item = qobject_cast<QQuickItem *>(object);
  if (!item) {
    if (component->isError())
      qWarning() << component->errors();
    delete object;
    return false;
  }
  QQuickItem *root = window()->contentItem();
  item->setParentItem(root);
  item->setZ(1e6);
  item->setAcceptHoverEvents(true);
  item->installEventFilter(this);
  // The delegate sizes itself, or leaves it to its implicit size.
  if (item->width() <= 0)
    item->setWidth(item->implicitWidth());
  if (item->height() <= 0)
    item->setHeight(item->implicitHeight());
  const QRectF bounds(QPointF(0, 0), root->size());
  m_popupAnchor = anchorFor(target);
  item->setPosition(qce::placePopup(mapRectToItem(root, m_popupAnchor), item->size(), bounds));
  m_popup = item;
  m_popupTarget = target;
  m_popupHovered = false;
  emit popupVisibleChanged();
  return true;
}

bool CodeEditor::popupHasFocus() const {
  QQuickItem *focused = window() ? window()->activeFocusItem() : nullptr;
  return m_popup && focused && (focused == m_popup.data() || m_popup->isAncestorOf(focused));
}

bool CodeEditor::showDiagnosticsAt(qsizetype offset) {
  const QList<qce::Diagnostic> found = m_diagnostics.at(offset);
  if (found.isEmpty())
    return false;
  HoverTarget target;
  target.kind = HoverTarget::Text;
  target.value = m_document.rope().offsetAt(found.first().start);
  target.offset = offset;
  return showPopup(target);
}

void CodeEditor::hidePopup() {
  m_hoverTimer.stop();
  m_popupGrace.stop();
  m_popupTarget = {};
  m_popupHovered = false;
  if (!m_popup)
    return;
  QQuickItem *item = m_popup;
  m_popup.clear();
  item->removeEventFilter(this);
  item->setVisible(false);
  item->setParentItem(nullptr); // gone from the scene now, deleted when the event loop gets to it
  item->deleteLater();
  emit popupVisibleChanged();
}

bool CodeEditor::eventFilter(QObject *watched, QEvent *event) {
  if (m_popup && watched == m_popup.data()) {
    if (event->type() == QEvent::HoverEnter) {
      m_popupHovered = true;
      m_popupGrace.stop();
    } else if (event->type() == QEvent::HoverLeave) {
      m_popupHovered = false;
      m_popupGrace.start();
    }
  }
  return QQuickItem::eventFilter(watched, event);
}

// What a popup could be about at `pos`: the character under the pointer, or the line of a gutter icon
// or of the end-of-line message the pointer is on.
CodeEditor::HoverTarget CodeEditor::popupTargetAt(const QPointF &pos) {
  if (pos.x() < 0 || pos.y() < 0 || pos.x() >= width() || pos.y() >= height())
    return {};
  const qsizetype row = qsizetype(std::floor((pos.y() + m_contentY) / m_metrics.lineHeight()));
  if (row < 0 || row >= m_map.rowCount())
    return {};
  const qce::DisplayRow displayRow = m_map.rowAt(row);
  if (pos.x() < m_gutterWidth)
    return qobject_cast<qce::DecorationColumn *>(columnAt(pos.x()))
             ? HoverTarget{HoverTarget::Line, displayRow.line, -1, m_gutterWidth}
             : HoverTarget{};
  const qce::TextSnapshot snapshot = m_document.snapshot();
  const auto layout = layoutForRow(displayRow, snapshot);
  const qreal x = pos.x() - m_gutterWidth + m_contentX;
  if (x < layout->indentX)
    return {};
  if (x < layout->indentX + layout->width) {
    // Cells run from one boundary to the next, so the boundary nearest half a cell to the left is the
    // start of the character under the pointer.
    const qsizetype column = qMax(displayRow.startColumn, columnForX(*layout, x - m_metrics.cellAdvance() / 2));
    const qsizetype offset = snapshot.rope().offsetAt({displayRow.line, column});
    // Text with no diagnostic on it is nothing to show a popup for, and the way to a popup crosses it.
    const QList<qce::Diagnostic> found = m_diagnostics.at(offset);
    if (found.isEmpty())
      return {};
    return {HoverTarget::Text, snapshot.rope().offsetAt(found.first().start), offset, 0};
  }
  // The end-of-line message: the popup hangs from where the pointer is, so it is on the way down.
  if (displayRow.isLast() && x < layout->indentX + layout->fullWidth && m_diagnostics.count() > 0)
    return {HoverTarget::Line, displayRow.line, -1, pos.x()};
  return {};
}

void CodeEditor::updatePopupHover(const QPointF &pos) {
  if (!m_diagnosticPopups || (m_diagnostics.count() == 0 && !m_popup))
    return;
  if (m_popup && window()) {
    // The pointer between what the popup hangs from and the popup, or on it, is on its way there (or
    // there): the box around both, with some slack, keeps it open however the pointer approaches, and
    // whatever lies under it. The slack is what a hand that overshoots by a few pixels needs.
    constexpr qreal kSlack = 12;
    const QRectF popupHere = mapRectFromItem(window()->contentItem(), m_popup->mapRectToItem(window()->contentItem(), m_popup->boundingRect()));
    if (m_popupAnchor.united(popupHere).adjusted(-kSlack, -kSlack, kSlack, kSlack).contains(pos)) {
      m_popupGrace.stop();
      return;
    }
  }
  const HoverTarget target = popupTargetAt(pos);
  if (m_popup && target == m_popupTarget) {
    m_popupGrace.stop();
    return;
  }
  if (target.kind == HoverTarget::None) {
    m_hoverTimer.stop();
    m_pendingTarget = {};
    if (m_popup && !m_popupHovered && !m_popupGrace.isActive())
      m_popupGrace.start();
    return;
  }
  if (target == m_pendingTarget && m_hoverTimer.isActive())
    return;
  hidePopup(); // the pointer moved on to something else
  m_pendingTarget = target;
  m_hoverTimer.start();
}

void CodeEditor::onHoverTimer() {
  if (m_pendingTarget.kind != HoverTarget::None)
    showPopup(m_pendingTarget);
}

// Decorations --------------------------------------------------------------------------------

static_assert(int(CodeEditor::Underline) == int(qce::DecorationKind::Underline));
static_assert(int(CodeEditor::Squiggle) == int(qce::DecorationKind::Squiggle));
static_assert(int(CodeEditor::Background) == int(qce::DecorationKind::Background));
static_assert(int(CodeEditor::GutterIcon) == int(qce::DecorationKind::GutterIcon));
static_assert(int(CodeEditor::EndOfLineText) == int(qce::DecorationKind::EndOfLineText));
static_assert(int(CodeEditor::InlineText) == int(qce::DecorationKind::InlineText));

namespace {
qce::Gravity gravityOption(const QVariant &value, qce::Gravity fallback) {
  const QString name = value.toString();
  if (name.compare(u"left", Qt::CaseInsensitive) == 0)
    return qce::Gravity::Left;
  if (name.compare(u"right", Qt::CaseInsensitive) == 0)
    return qce::Gravity::Right;
  return fallback;
}
} // namespace

int CodeEditor::addDecoration(qsizetype start, qsizetype end, const QVariantMap &options) {
  qce::DecorationSpec spec;
  spec.start = start;
  spec.end = end;
  const int kind = options.value(u"kind"_s, int(Underline)).toInt();
  if (kind < 0 || kind >= qce::kDecorationKindCount)
    return 0;
  spec.kind = qce::DecorationKind(kind);
  spec.color = options.value(u"color"_s).value<QColor>();
  spec.text = options.value(u"text"_s).toString();
  spec.severity = options.value(u"severity"_s, 0).toInt();
  spec.priority = options.value(u"priority"_s, 0).toInt();
  spec.tag = options.value(u"tag"_s, 0).toInt();
  spec.startGravity = gravityOption(options.value(u"startGravity"_s), spec.startGravity);
  spec.endGravity = gravityOption(options.value(u"endGravity"_s), spec.endGravity);
  return m_decorations.add(spec, options.value(u"layer"_s, 0).toInt());
}

bool CodeEditor::removeDecoration(int id) { return m_decorations.remove(id); }

void CodeEditor::clearDecorations(int layer) { m_decorations.clearLayer(layer); }

void CodeEditor::setDiagnostics(const QVariantList &diagnostics) {
  QList<qce::Diagnostic> list;
  list.reserve(diagnostics.size());
  for (const QVariant &item : diagnostics)
    list.append(qce::Diagnostic::fromLsp(item.toMap()));
  m_diagnostics.setDiagnostics(list);
}

void CodeEditor::setDiagnostics(const QList<qce::Diagnostic> &diagnostics) { m_diagnostics.setDiagnostics(diagnostics); }

void CodeEditor::clearDiagnostics() { m_diagnostics.clear(); }

void CodeEditor::setInlayHints(const QVariantList &hints) {
  QList<qce::InlayHint> list;
  list.reserve(hints.size());
  for (const QVariant &item : hints)
    list.append(qce::InlayHint::fromLsp(item.toMap()));
  m_decorations.setLayer(qce::kInlayLayer, qce::inlayHintSpecs(list, m_document.rope()));
  emit inlayHintsChanged();
}

void CodeEditor::clearInlayHints() {
  m_decorations.clearLayer(qce::kInlayLayer);
  emit inlayHintsChanged();
}

QVariantList CodeEditor::diagnosticsAt(qsizetype offset) const {
  QVariantList list;
  for (const qce::Diagnostic &d : m_diagnostics.at(offset))
    list.append(d.toLsp());
  return list;
}

bool CodeEditor::gotoDiagnostic(bool forward, int leastSevere) {
  const qsizetype from = cursorPosition();
  const std::optional<qce::Diagnostic> found =
    forward ? m_diagnostics.next(from, true, leastSevere) : m_diagnostics.previous(from, true, leastSevere);
  if (!found)
    return false;
  // A cursor placed in folded text opens the fold (revealCursor); select() does not scroll.
  const qsizetype offset = m_document.rope().offsetAt(found->start);
  setCursorPosition(offset);
  ensureCursorVisible();
  if (m_diagnosticPopups)
    showDiagnosticsAt(offset);
  return true;
}

void CodeEditor::setDiagnosticMessages(DiagnosticMessages messages) {
  if (messages == diagnosticMessages())
    return;
  m_diagnostics.setEndOfLineMessages(messages == EndOfLineMessages);
  emit diagnosticMessagesChanged();
}

void CodeEditor::onDecorationsChanged(qsizetype firstLine, qsizetype lastLine, quint32 kinds) {
  // Virtual text is part of a row's layout, so the rows it was added to or removed from are laid out
  // again; the rest only changes what is drawn over them.
  hidePopup(); // what it shows may be gone
  constexpr quint32 inLayout =
    qce::decorationKindBit(qce::DecorationKind::EndOfLineText) | qce::decorationKindBit(qce::DecorationKind::InlineText);
  if (kinds & inLayout) {
    const qsizetype count = lastLine - firstLine + 1;
    m_layouts.invalidate(firstLine, count, count);
  }
  invalidatePlan();
}

// The text to show after the end of `line`: the highest-priority end-of-line decoration that starts
// on it, cut at its first line break.
QString CodeEditor::endOfLineText(qsizetype line, QColor *color) const {
  const qce::Rope &rope = m_document.rope();
  const qsizetype lineStart = rope.lineStart(line), lineEnd = rope.lineEnd(line);
  const QList<qce::Decoration> found =
    m_decorations.query(lineStart, lineEnd, qce::decorationKindBit(qce::DecorationKind::EndOfLineText));
  const qce::Decoration *best = nullptr;
  for (const qce::Decoration &d : found)
    if (d.start >= lineStart && d.start <= lineEnd && (!best || d.priority > best->priority))
      best = &d;
  if (!best)
    return {};
  QString text = best->text;
  for (qsizetype i = 0; i < text.size(); ++i)
    if (text[i] == u'\n' || text[i] == u'\r') {
      text.truncate(i);
      break;
    }
  if (color)
    *color = best->color.isValid() ? best->color
              : best->severity != 0 ? m_theme->severityColor(best->severity)
                                    : m_theme->virtualText();
  return text;
}

// Turns the decorations of the rows in the plan into colored spans: backgrounds go behind the text,
// underlines and squiggles in front of it. Like selections, only what reaches the plan is looked at,
// however many decorations there are.
void CodeEditor::buildDecorations() {
  m_decoBackgroundSpans.clear();
  m_decoUnderlineSpans.clear();
  m_squiggleSpans.clear();
  using qce::DecorationKind;
  if (m_planLast < m_planFirst || m_planFirst < 0)
    return;
  const qreal lineHeight = m_metrics.lineHeight();
  // The pills behind inline virtual text, wherever the rows of the plan have some.
  if (m_decorations.count(DecorationKind::InlineText) > 0) {
    for (const qce::FramePlanRow &planRow : std::as_const(m_plan)) {
      const qce::LineLayout &layout = *planRow.layout;
      for (const qce::Injection &injection : layout.injections) {
        if (!injection.pill)
          continue;
        const QTextLine textLine = layout.layout->lineAt(0);
        m_decoBackgroundSpans.append(
          {planRow.row, layout.indentX + textLine.cursorToX(injection.start),
           layout.indentX + textLine.cursorToX(injection.start + injection.length), 1, lineHeight - 2,
           m_theme->inlayHintBackground()}
        );
      }
    }
  }
  if (m_theme->hasStyleBackgrounds()) {
    for (const qce::FramePlanRow &planRow : std::as_const(m_plan)) {
      const qce::LineLayout &layout = *planRow.layout;
      for (const qce::StyleBackground &b : layout.styleBackgrounds) {
        const qreal x0 = xForColumn(layout, layout.startColumn + b.start),
                    x1 = xForColumn(layout, layout.startColumn + b.end);
        if (x1 > x0)
          m_decoBackgroundSpans.append({planRow.row, x0, x1, 0, -1, b.color});
      }
    }
  }
  buildBracketMatches();
  buildSearchMatches();
  buildIndentGuides();
  const qsizetype drawn = m_decorations.count(DecorationKind::Underline) + m_decorations.count(DecorationKind::Squiggle) +
                          m_decorations.count(DecorationKind::Background);
  if (drawn == 0)
    return;
  constexpr qsizetype kMaxSpans = 20000;
  const qce::Rope &rope = m_document.rope();
  const qsizetype firstLine = m_map.rowAt(m_planFirst).line;
  const qsizetype afterLine = m_map.folds().nextVisibleLine(m_map.rowAt(m_planLast).line);
  const qsizetype lowOffset = rope.lineStart(firstLine);
  const qsizetype highOffset = afterLine < rope.lineCount() ? rope.lineStart(afterLine) : rope.length();
  QList<qce::Decoration> found = m_decorations.query(
    lowOffset, highOffset,
    qce::decorationKindBit(DecorationKind::Underline) | qce::decorationKindBit(DecorationKind::Squiggle) |
      qce::decorationKindBit(DecorationKind::Background)
  );
  // Lowest priority first, so higher ones paint over them.
  std::stable_sort(found.begin(), found.end(), [](const auto &a, const auto &b) { return a.priority < b.priority; });

  const qreal cell = m_metrics.cellAdvance(), lh = m_metrics.lineHeight();
  const qreal viewLeft = m_contentX - cell, viewRight = m_contentX + textViewportWidth() + cell;
  const qreal underlineY = qMin(lh - 1, std::round(m_metrics.ascent()) + 1);
  const qreal squiggleHeight = qMin<qreal>(3, lh / 4);
  const qsizetype lineCountNow = rope.lineCount();
  for (const qce::Decoration &d : std::as_const(found)) {
    qce::TextPosition start = rope.positionAt(d.start);
    qce::TextPosition end = rope.positionAt(d.end);
    const bool startHidden = m_map.folds().isHidden(start.line);
    if (startHidden && m_map.folds().isHidden(end.line))
      continue; // all of it is folded away
    if (startHidden) { // the part after the fold
      const qsizetype next = m_map.folds().nextVisibleLine(start.line);
      if (next >= lineCountNow)
        continue;
      start = {next, 0};
    }
    start = m_map.visiblePosition(start);
    end = m_map.visiblePosition(end);
    const qsizetype startRow = m_map.rowForPosition(start);
    const qsizetype endRow = m_map.rowForPosition(end);
    const QColor color = d.color.isValid() ? d.color : m_theme->severityColor(d.severity);
    const bool empty = d.start == d.end;
    for (qsizetype row = qMax(startRow, m_planFirst); row <= qMin(endRow, m_planLast); ++row) {
      const qce::LineLayout &layout = *m_plan[row - m_planFirst].layout;
      const qreal x0 = row == startRow ? xForColumn(layout, start.column) : layout.indentX;
      qreal x1 = row == endRow ? xForColumn(layout, end.column) : layout.indentX + layout.width;
      if (x1 <= x0) {
        // An empty range still gets a mark one cell wide (but not a background).
        if (!empty || d.kind == DecorationKind::Background)
          continue;
        x1 = x0 + cell;
      }
      if (x1 < viewLeft || x0 > viewRight)
        continue;
      switch (d.kind) {
      case DecorationKind::Background:
        m_decoBackgroundSpans.append({row, x0, x1, 0, -1, color});
        break;
      case DecorationKind::Underline:
        m_decoUnderlineSpans.append({row, x0, x1, underlineY, 1, color});
        break;
      case DecorationKind::Squiggle:
        m_squiggleSpans.append({row, x0, x1, lh - squiggleHeight, squiggleHeight, color});
        break;
      default:
        break;
      }
    }
    if (m_decoBackgroundSpans.size() + m_decoUnderlineSpans.size() + m_squiggleSpans.size() >= kMaxSpans)
      break;
  }
}

// Backgrounds for the bracket next to each cursor and its partner. Only cursors in the rows being
// drawn are looked at (the selections are sorted), each lookup is cached until the next edit, and
// the number of cursors is capped so a thousand of them cost one frame, not a scan each.
void CodeEditor::buildBracketMatches() {
  if (!m_matchBrackets || m_autoClosePairs.isEmpty())
    return;
  constexpr int kMaxCursors = 1000;
  const qce::Rope &rope = m_document.rope();
  const qsizetype firstLine = m_map.rowAt(m_planFirst).line;
  const qsizetype afterLine = m_map.folds().nextVisibleLine(m_map.rowAt(m_planLast).line);
  const qsizetype lowOffset = rope.lineStart(firstLine);
  const qsizetype highOffset = afterLine < rope.lineCount() ? rope.lineStart(afterLine) : rope.length();
  const qreal cell = m_metrics.cellAdvance();
  const qreal viewLeft = m_contentX - cell, viewRight = m_contentX + textViewportWidth() + cell;
  const QColor color = m_theme->bracketMatch();

  QList<qsizetype> drawn;
  auto mark = [&](qsizetype offset) {
    if (drawn.contains(offset))
      return;
    drawn.append(offset);
    qce::TextPosition position = rope.positionAt(offset);
    if (m_map.folds().isHidden(position.line))
      return;
    position = m_map.visiblePosition(position);
    const qsizetype row = m_map.rowForPosition(position);
    if (row < m_planFirst || row > m_planLast)
      return;
    const qce::LineLayout &layout = *m_plan[row - m_planFirst].layout;
    const qreal x0 = xForColumn(layout, position.column);
    const qreal x1 = xForColumn(layout, position.column + 1);
    if (x1 > x0 && x1 >= viewLeft && x0 <= viewRight)
      m_decoBackgroundSpans.append({row, x0, x1, 0, -1, color});
  };

  int cursors = 0;
  for (int i = m_selections.lowerBound(lowOffset); i < m_selections.count() && cursors < kMaxCursors; ++i) {
    const qce::Selection sel = m_selections.at(i);
    if (sel.start() > highOffset)
      break;
    if (sel.head < lowOffset || sel.head > highOffset)
      continue;
    ++cursors;
    const qce::BracketPair pair = bracketPairAt(sel.head);
    if (pair.valid()) {
      mark(pair.open);
      mark(pair.close);
    }
  }
}

// Backgrounds for the matches of the search pattern in the rows being drawn. Each row is matched on
// its own text, so a match that wraps across rows is not marked and the work is bounded by the rows
// in the frame plan.
void CodeEditor::buildSearchMatches() {
  if (m_searchHighlight.pattern().isEmpty() || !m_searchHighlight.isValid())
    return;
  constexpr int kMaxMatches = 4000;
  const qreal cell = m_metrics.cellAdvance();
  const qreal viewLeft = m_contentX - cell, viewRight = m_contentX + textViewportWidth() + cell;
  const QColor color = m_theme->searchMatch();
  int count = 0;
  for (qsizetype row = m_planFirst; row <= m_planLast && count < kMaxMatches; ++row) {
    const qce::LineLayout *layout = m_plan[row - m_planFirst].layout.get();
    if (!layout || layout->text.isEmpty())
      continue;
    QRegularExpressionMatchIterator it = m_searchHighlight.globalMatch(layout->text);
    while (it.hasNext() && count < kMaxMatches) {
      const QRegularExpressionMatch match = it.next();
      if (match.capturedLength() == 0)
        continue;
      const qreal x0 = xForColumn(*layout, layout->startColumn + match.capturedStart());
      const qreal x1 = xForColumn(*layout, layout->startColumn + match.capturedEnd());
      if (x1 > x0 && x1 >= viewLeft && x0 <= viewRight) {
        m_decoBackgroundSpans.append({row, x0, x1, 0, -1, color});
        ++count;
      }
    }
  }
}

// The pair of the bracket next to a cursor at `head`, cached until the next edit.
qce::BracketPair CodeEditor::bracketPairAt(qsizetype head) {
  constexpr int kMaxCached = 4096;
  if (const auto cached = m_bracketCache.constFind(head); cached != m_bracketCache.constEnd())
    return *cached;
  if (m_bracketCache.size() >= kMaxCached)
    m_bracketCache.clear();
  qce::BracketPair pair;
  const qce::Rope &rope = m_document.rope();
  if (const qsizetype at = qce::bracketNearCursor(rope, head, m_autoClosePairs); at >= 0)
    pair = qce::findMatchingBracket(rope, at, m_autoClosePairs);
  m_bracketCache.insert(head, pair);
  return pair;
}

// Guides for the rows of the plan, one per indent step inside the leading whitespace. The block
// around the primary cursor gets the active color on the lines strictly between its brackets.
void CodeEditor::buildIndentGuides() {
  m_guideSpans.clear();
  m_activeGuideSpans.clear();
  if (!m_showIndentGuides)
    return;
  constexpr qsizetype kMaxGuides = 4000;
  const int unit = m_insertSpaces ? m_indentWidth : m_metrics.tabWidth();
  if (unit <= 0 || m_plan.isEmpty())
    return;
  const qce::Rope &rope = m_document.rope();
  const qreal cell = m_metrics.cellAdvance();
  const qreal viewLeft = m_contentX - 2, viewRight = m_contentX + textViewportWidth() + 2;

  // The block of the active guide: the pair next to the primary cursor, else the one around it.
  qsizetype activeFrom = -1, activeTo = -1; // buffer lines strictly between the brackets, inclusive
  int activeColumn = -1;
  if (!m_autoClosePairs.isEmpty()) {
    const qsizetype head = m_selections.primary().head;
    if (!m_activeBlockCache || m_activeBlockCache->first != head) {
      qce::BracketPair pair = bracketPairAt(head);
      if (!pair.valid())
        pair = qce::findEnclosingBrackets(rope, head, m_autoClosePairs);
      m_activeBlockCache = std::make_pair(head, pair);
    }
    if (const qce::BracketPair pair = m_activeBlockCache->second; pair.valid()) {
      const qsizetype openLine = rope.lineAt(pair.open), closeLine = rope.lineAt(pair.close);
      if (closeLine - openLine > 1) {
        const QList<int> opener = qce::effectiveIndents(rope, openLine, openLine, m_metrics.tabWidth(), unit);
        activeFrom = openLine + 1;
        activeTo = closeLine - 1;
        activeColumn = opener.first() / unit * unit;
      }
    }
  }

  // Guides of neighbouring rows join into one tall rectangle per column, so a screenful costs a few
  // dozen scene-graph nodes rather than one per row and level.
  struct Run {
    qsizetype first, last;
    qreal x;
    bool active;
  };
  QHash<int, Run> open; // by column * 2 + active
  const qreal lineHeight = m_metrics.lineHeight();
  auto flush = [&](const Run &run) {
    auto &spans = run.active ? m_activeGuideSpans : m_guideSpans;
    spans.append({run.first, run.x, run.x + 1, 0, qreal(run.last - run.first + 1) * lineHeight});
  };

  // Indents are worked out per run of consecutive buffer lines, so a blank line sees the text around
  // it and a fold doesn't make the range as long as the lines it hides.
  qsizetype runFirst = 0;
  QList<int> run;
  for (qsizetype i = 0; i < m_plan.size(); ++i) {
    const qce::FramePlanRow &planRow = m_plan[i];
    const qsizetype line = planRow.display.line;
    if (run.isEmpty() || line < runFirst || line >= runFirst + run.size()) {
      qsizetype last = line;
      for (qsizetype j = i + 1; j < m_plan.size(); ++j) {
        const qsizetype next = m_plan[j].display.line;
        if (next > last + 1)
          break;
        last = qMax(last, next);
      }
      runFirst = line;
      run = qce::effectiveIndents(rope, runFirst, last, m_metrics.tabWidth(), unit);
    }
    const int indent = run[line - runFirst];
    const bool active = line >= activeFrom && line <= activeTo;
    const qreal rowIndent = planRow.layout->indentX;
    QList<int> touched;
    for (int column = 0; column < indent; column += unit) {
      const qreal x = qRound(column * cell);
      if (!planRow.display.isFirst() && x >= rowIndent)
        break;
      if (x + 1 < viewLeft)
        continue;
      if (x > viewRight)
        break;
      const int key = column * 2 + (active && column == activeColumn ? 1 : 0);
      touched.append(key);
      if (auto it = open.find(key); it != open.end() && it->last == planRow.row - 1)
        it->last = planRow.row;
      else {
        if (it != open.end())
          flush(*it);
        open.insert(key, {planRow.row, planRow.row, x, (key & 1) != 0});
      }
    }
    for (auto it = open.begin(); it != open.end();) {
      if (touched.contains(it.key())) {
        ++it;
        continue;
      }
      flush(*it);
      it = open.erase(it);
    }
    if (m_guideSpans.size() + m_activeGuideSpans.size() >= kMaxGuides)
      return;
  }
  for (const Run &r : std::as_const(open))
    flush(r);
}

CodeEditor::RenderStats CodeEditor::renderStats() const {
  return {m_layouts.stats().created, m_layouts.stats().hits, m_layouts.size(), m_plan.size(),
          m_polishCalls,             m_polishNs,                m_polishMaxNs,   m_sceneStats};
}

void CodeEditor::onThemeChanged() { invalidateLayouts(); }

void CodeEditor::setText(const QString &text) { m_document.setText(text); }

void CodeEditor::load(const QUrl &file) {
  const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
  m_loadProgress = 0;
  emit loadProgressChanged();
  m_document.load(path);
  emit loadingChanged();
}

void CodeEditor::save(const QUrl &file) {
  const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
  struct Outcome {
    bool ok;
    QString error;
  };
  auto *watcher = new QFutureWatcher<Outcome>(this);
  connect(watcher, &QFutureWatcher<Outcome>::finished, this, [this, watcher, path] {
    const Outcome outcome = watcher->result();
    if (outcome.ok)
      emit saved(path);
    else
      emit saveFailed(outcome.error);
    watcher->deleteLater();
  });
  watcher->setFuture(QtConcurrent::run([rope = m_document.snapshot().rope(), path, format = m_document.format()] {
    QString error;
    const bool ok = qce::saveFile(rope, path, format, &error);
    return Outcome{ok, error};
  }));
}

void CodeEditor::onDocumentReset() {
  if (!m_document.isLoading())
    applyDetectedIndentation();
  updateUndoState();
  m_lastLineCount = lineCount();
  m_layouts.clear();
  m_bracketCache.clear();
  m_activeBlockCache.reset();
  m_maxLineWidth = 0;
  emit lineCountChanged();
  updateContentSize();
  captureAnchor();
  invalidatePlan();
}

void CodeEditor::onDocumentChanged(const qce::TextChange &change) {
  hidePopup();
  m_bracketCache.clear();
  m_activeBlockCache.reset();
  const qsizetype first = change.startPos.line;
  m_layouts.invalidate(first, change.oldEndPos.line - first + 1, change.newEndPos.line - first + 1);
  if (const qsizetype count = lineCount(); count != m_lastLineCount) {
    m_lastLineCount = count;
    emit lineCountChanged();
  }
  updateContentSize();
  invalidatePlan();
  updateUndoState();
}

namespace {

// Adds a `color` format to every run of spaces and tabs that the highlighter's ranges (sorted,
// non-overlapping) don't already style, so indentation and gaps read as dim marks while
// whitespace inside strings or comments keeps its token color.
QList<QTextLayout::FormatRange>
withWhitespaceFormats(const QString &text, QList<QTextLayout::FormatRange> ranges, const QColor &color) {
  QTextCharFormat format;
  format.setForeground(color);
  QList<QTextLayout::FormatRange> result;
  result.reserve(ranges.size() + 4);
  qsizetype next = 0; // first range not yet passed
  qsizetype i = 0;
  const qsizetype n = text.size();
  while (i < n) {
    const QChar c = text[i];
    if (c != u' ' && c != u'\t') {
      ++i;
      continue;
    }
    qsizetype j = i;
    while (j < n && (text[j] == u' ' || text[j] == u'\t'))
      ++j;
    // Copy over styled ranges that start before this run ends, then add the uncovered pieces.
    qsizetype pos = i;
    while (next < ranges.size() && ranges[next].start < j) {
      const auto &r = ranges[next];
      if (r.start + r.length > i) {
        if (r.start > pos)
          result.append({int(pos), int(r.start - pos), format});
        pos = qMax<qsizetype>(pos, r.start + r.length);
      }
      result.append(r);
      ++next;
    }
    if (pos < j)
      result.append({int(pos), int(j - pos), format});
    i = j;
  }
  while (next < ranges.size())
    result.append(ranges[next++]);
  std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) { return a.start < b.start; });
  return result;
}

// Makes room for injected text: format ranges after an injection move right by its length, one that
// straddles an injection is split around it, and the injections' own formats (positions already in
// the laid-out text) are added.
QList<QTextLayout::FormatRange> withInjections(
  const QList<QTextLayout::FormatRange> &ranges, const QList<qce::Injection> &injections,
  const QList<QTextLayout::FormatRange> &injected
) {
  QList<QTextLayout::FormatRange> out;
  out.reserve(ranges.size() + injected.size() + injections.size());
  for (const QTextLayout::FormatRange &r : ranges) {
    const int end = r.start + r.length;
    int from = r.start;
    auto addPiece = [&](int pieceStart, int pieceEnd) {
      int shift = 0;
      for (const qce::Injection &injection : injections)
        if (injection.column <= pieceStart)
          shift += injection.length;
      out.append({pieceStart + shift, pieceEnd - pieceStart, r.format});
    };
    for (const qce::Injection &injection : injections) {
      if (injection.column > from && injection.column < end) {
        addPiece(from, injection.column);
        from = injection.column;
      }
    }
    addPiece(from, end);
  }
  out += injected;
  std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.start < b.start; });
  return out;
}

} // namespace

namespace {

// The part of a line's spans that falls inside columns [start, end), relative to start.
QList<qce::HighlightSpan> sliceSpans(const QList<qce::HighlightSpan> &spans, qsizetype start, qsizetype end) {
  if (start == 0)
    return spans; // spans past `end` are harmless: QTextLayout clips formats to the text
  QList<qce::HighlightSpan> out;
  for (qce::HighlightSpan span : spans) {
    const qsizetype from = qMax(span.start, start), to = qMin(span.start + span.length, end);
    if (to <= from)
      continue;
    span.start = from - start;
    span.length = to - from;
    out.append(span);
  }
  return out;
}

} // namespace

std::shared_ptr<qce::LineLayout>
CodeEditor::layoutForRow(const qce::DisplayRow &row, const qce::TextSnapshot &snapshot) {
  if (auto cached = m_layouts.find(row.line, row.rowInLine))
    return cached;

  const qce::Rope &rope = snapshot.rope();
  const qsizetype lineStart = rope.lineStart(row.line);
  const QString text = rope.toString(lineStart + row.startColumn, lineStart + row.endColumn);
  // Visible spaces are drawn as middle dots: the text node doesn't render QTextOption's own marks.
  // Dot and space share a column in a monospace font, so offsets and positions stay the same.
  QString display = text;
  if (m_showWhitespace)
    display.replace(u' ', u'\u00b7');
  auto layout = std::make_unique<QTextLayout>(display, m_metrics.layoutFont());
  QTextOption option;
  option.setWrapMode(QTextOption::NoWrap);
  option.setTabStopDistance(m_metrics.tabWidth() * m_metrics.cellAdvance());
  layout->setTextOption(option);
  layout->setCacheEnabled(true);
  auto spans = m_highlighter->highlightLines(snapshot, row.line, row.line);
  if (spans.isEmpty())
    spans.append(QList<qce::HighlightSpan>());
  for (qce::Highlighter *overlay : std::as_const(m_overlays)) {
    const auto over = overlay->highlightLines(snapshot, row.line, row.line);
    if (!over.isEmpty() && !over.first().isEmpty())
      spans.first() = qce::overlaySpans(spans.first(), over.first());
  }
  const QList<qce::HighlightSpan> rowSpans = sliceSpans(spans.first(), row.startColumn, row.endColumn);
  QList<QTextLayout::FormatRange> formats = m_theme->formatRanges(rowSpans);
  // Token styles with a background (API-13): the backdrop draws them, once the layout says where.
  QList<qce::StyleBackground> styleBackgrounds;
  if (m_theme->hasStyleBackgrounds()) {
    const int rowLength = int(text.size());
    for (const qce::HighlightSpan &span : rowSpans) {
      const QColor color = m_theme->styleBackground(span.style);
      const int from = int(qMax<qsizetype>(span.start, 0)), to = int(qMin<qsizetype>(span.start + span.length, rowLength));
      if (!color.isValid() || to <= from)
        continue;
      if (!styleBackgrounds.isEmpty() && styleBackgrounds.last().end == from && styleBackgrounds.last().color == color)
        styleBackgrounds.last().end = to;
      else
        styleBackgrounds.append({from, to, color});
    }
  }
  if (m_showWhitespace)
    formats = withWhitespaceFormats(text, std::move(formats), m_theme->whitespace());
  // Text that is shown in the row without being in the document: inline decorations (inlay hints) and
  // an input-method composition. Each is put into the laid-out text before the unit at its column.
  struct Pending {
    qce::Injection injection;
    QString text;
    QList<QTextLayout::FormatRange> formats; // relative to the start of `text`
  };
  QList<Pending> pending;
  if (m_decorations.count(qce::DecorationKind::InlineText) > 0) {
    const qsizetype rowStart = lineStart + row.startColumn, rowEnd = lineStart + row.endColumn;
    const QList<qce::Decoration> hints =
      m_decorations.query(rowStart, rowEnd, qce::decorationKindBit(qce::DecorationKind::InlineText));
    for (const qce::Decoration &hint : hints) {
      if (hint.start < rowStart || hint.start > rowEnd || hint.text.isEmpty())
        continue;
      const int column = int(hint.start - rowStart);
      // A hint belongs to the row that has the text it leans on, which decides where it goes at a
      // soft break (the wrap code makes the same choice).
      const bool leansForward = hint.startGravity == qce::Gravity::Left;
      if (!leansForward && column == 0 && row.startColumn > 0)
        continue;
      if (leansForward && column == row.endColumn - row.startColumn && !row.isLast())
        continue;
      QTextCharFormat format;
      format.setForeground(hint.color.isValid() ? hint.color : m_theme->inlayHint());
      Pending item;
      item.injection.column = column;
      item.injection.placement = leansForward ? qce::Injection::BeforeCursor : qce::Injection::AfterCursor;
      item.injection.pill = true;
      item.text = hint.text;
      item.formats.append({0, int(hint.text.size()), format});
      pending.append(std::move(item));
    }
  }
  if (hasPreedit()) {
    const qce::TextPosition at = rope.positionAt(m_document.anchors().offset(m_preeditAnchor));
    // A position at a soft break belongs to the row after it; the last row also owns the line end.
    if (at.line == row.line && at.column >= row.startColumn &&
        (at.column < row.endColumn || (row.isLast() && at.column <= row.endColumn))) {
      Pending item;
      item.injection.column = int(at.column - row.startColumn);
      item.injection.placement = qce::Injection::Preedit;
      item.text = m_preedit;
      item.formats = m_preeditFormats;
      pending.append(std::move(item));
    }
  }
  QList<qce::Injection> injections;
  if (!pending.isEmpty()) {
    std::stable_sort(pending.begin(), pending.end(), [](const Pending &a, const Pending &b) {
      return std::pair(a.injection.column, a.injection.placement) < std::pair(b.injection.column, b.injection.placement);
    });
    QString full;
    QList<QTextLayout::FormatRange> injectedFormats;
    int taken = 0; // units of `display` copied so far
    for (Pending &item : pending) {
      full += display.mid(taken, item.injection.column - taken);
      taken = item.injection.column;
      item.injection.start = int(full.size());
      item.injection.length = int(item.text.size());
      full += item.text;
      for (QTextLayout::FormatRange r : std::as_const(item.formats)) {
        r.start += item.injection.start;
        injectedFormats.append(r);
      }
      injections.append(item.injection);
    }
    full += display.mid(taken);
    layout->setText(full);
    formats = withInjections(std::move(formats), injections, injectedFormats);
  }
  // End-of-line virtual text follows the row's own text after a gap (DIAG-01). It is part of the
  // layout so it is drawn, clipped and measured with the row, but not part of the row's text: columns,
  // hit-testing and selections stop where the text does.
  QString trailing;
  QColor trailingColor;
  if (row.isLast() && m_decorations.count(qce::DecorationKind::EndOfLineText) > 0)
    trailing = endOfLineText(row.line, &trailingColor);
  constexpr int kTrailingGap = 2;
  const QString baseText = layout->text();
  const int baseLength = int(baseText.size());
  // Lays the row out with `extra` after the text; returns the width of the text and of everything.
  auto layOut = [&](const QString &extra) {
    QList<QTextLayout::FormatRange> all = formats;
    if (!extra.isEmpty()) {
      layout->setText(baseText + QString(kTrailingGap, u' ') + extra);
      QTextCharFormat format;
      format.setForeground(trailingColor);
      format.setFontItalic(true);
      all.append({baseLength + kTrailingGap, int(extra.size()), format});
    }
    if (!all.isEmpty())
      layout->setFormats(all);
    layout->beginLayout();
    QTextLine textLine = layout->createLine();
    textLine.setLineWidth(1e9);
    textLine.setPosition(QPointF(0, 0));
    const qreal full = textLine.naturalTextWidth();
    layout->endLayout();
    return std::pair(extra.isEmpty() ? full : layout->lineAt(0).cursorToX(baseLength), full);
  };
  auto [width, fullWidth] = layOut(trailing);
  if (!trailing.isEmpty() && m_map.wrapEnabled()) {
    // A wrapped row has nowhere to put text that does not fit: cut it short instead of letting it
    // run under the edge.
    const qreal room = textViewportWidth() - row.indent - width - kTrailingGap * m_metrics.cellAdvance();
    const qsizetype fit = qsizetype(std::floor(room / m_metrics.cellAdvance()));
    if (trailing.size() > fit) {
      trailing = fit >= 2 ? trailing.left(fit - 1) + QChar(0x2026) : QString();
      layout->clearLayout();
      if (trailing.isEmpty())
        layout->setText(baseText);
      std::tie(width, fullWidth) = layOut(trailing);
    }
  }
  auto result = m_layouts.insert(row.line, std::move(layout), width, text, row.rowInLine);
  result->fullWidth = fullWidth;
  result->startColumn = row.startColumn;
  result->indentX = row.indent;
  result->endsLine = row.isLast();
  result->injections = std::move(injections);
  result->styleBackgrounds = std::move(styleBackgrounds);
  return result;
}

// Lays out the viewport plus a margin of rows on each side. Nothing outside that window is touched,
// however large the document is.
void CodeEditor::updatePolish() {
  QElapsedTimer polishTimer;
  polishTimer.start();
  struct Record {
    CodeEditor *self;
    QElapsedTimer &timer;
    ~Record() {
      const quint64 ns = quint64(timer.nsecsElapsed());
      ++self->m_polishCalls;
      self->m_polishNs += ns;
      self->m_polishMaxNs = qMax(self->m_polishMaxNs, ns);
    }
  } record{this, polishTimer};
  // The gutter's width is part of what wrapping and scrolling are laid out against, so it comes first.
  const bool gutterMoved = m_planDirty && updateGutterLayout();
  if (gutterMoved) {
    m_wrapDirty = true;
    m_planDirty = true;
  }
  if (m_wrapDirty)
    applyWrap();
  if (gutterMoved)
    updateContentSizeKeepingAnchor();
  if (m_reanchorPending) {
    // Wrapping showed some lines to be taller or shorter than assumed: the text at the top of the
    // view stays where it was and the row number it is on moves.
    m_reanchorPending = false;
    updateContentSizeKeepingAnchor();
    m_planDirty = true;
  }
  const qsizetype rowCount = m_map.rowCount();
  const qreal lineHeight = m_metrics.lineHeight();
  const qsizetype visibleRows = qsizetype(std::ceil(height() / lineHeight)) + 1;
  const qsizetype margin = qMax<qsizetype>(4, visibleRows / 2);
  const qsizetype top = qsizetype(std::floor(m_contentY / lineHeight));
  const qsizetype firstRow = qBound<qsizetype>(0, top - margin, rowCount - 1);
  const qsizetype lastRow = qBound<qsizetype>(0, top + visibleRows + margin, rowCount - 1);

  // Plain scrolling inside the layout window changes nothing but the scroll transform.
  if (!m_planDirty && firstRow == m_planFirst && lastRow == m_planLast) {
    scrollGutter();
    update();
    return;
  }

  m_plan.clear();
  m_layouts.setCapacity(qMax<qsizetype>(256, 3 * (lastRow - firstRow + 1)));
  const qce::TextSnapshot snapshot = m_document.snapshot();
  m_plan.reserve(lastRow - firstRow + 1);
  qreal widest = m_maxLineWidth;
  const bool hasFolds = m_map.folds().hasFolds();
  for (qsizetype row = firstRow; row <= lastRow; ++row) {
    const qce::DisplayRow displayRow = m_map.rowAt(row);
    auto layout = layoutForRow(displayRow, snapshot);
    widest = qMax(widest, layout->indentX + layout->fullWidth);
    if (hasFolds && displayRow.isLast() && m_map.folds().isFolded(displayRow.line))
      widest = qMax(widest, layout->indentX + layout->fullWidth + 5 * m_metrics.cellAdvance());
    m_plan.append({row, std::move(layout), displayRow});
  }
  m_planFirst = firstRow;
  m_planLast = lastRow;
  m_planDirty = false;
  buildOverlays();
  buildDecorations();
  buildGutter();
  m_maxLineWidth = widest;
  updateContentSize();
  update();
}

void CodeEditor::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) {
  QQuickItem::geometryChange(newGeometry, oldGeometry);
  if (newGeometry.size() != oldGeometry.size()) {
    if (m_wrapMode == WrapAtViewport && newGeometry.width() != oldGeometry.width())
      invalidateWrap();
    updateContentSize();
    invalidatePlan();
  }
}

QSGNode *CodeEditor::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) {
  auto *scene = static_cast<qce::EditorScene *>(oldNode);
  if (!scene)
    scene = new qce::EditorScene(window());

  qce::FrameParams params;
  params.viewport = boundingRect();
  params.background = m_theme->background();
  params.foreground = m_theme->foreground();
  params.lineHeight = m_metrics.lineHeight();
  params.contentX = m_contentX;
  params.contentY = m_contentY;
  params.rows = &m_plan;
  params.currentLineColor = m_theme->currentLine();
  params.selectionColor = m_theme->selection();
  params.cursorColor = m_theme->cursor();
  if (m_handler->cursorShape() == qce::CursorShape::Block)
    params.cursorColor.setAlpha(150); // the character stays readable under the block
  params.currentLine = &m_currentLineSpans;
  params.selection = &m_selectionSpans;
  params.markColor = m_theme->whitespace();
  params.marks = &m_markSpans;
  params.chipColor = m_theme->foldPlaceholder();
  params.chips = &m_chipSpans;
  params.chipDotColor = m_theme->foldPlaceholderText();
  params.chipDots = &m_chipDotSpans;
  params.decorationBackgrounds = &m_decoBackgroundSpans;
  params.decorationUnderlines = &m_decoUnderlineSpans;
  params.squiggles = &m_squiggleSpans;
  params.devicePixelRatio = window() ? window()->effectiveDevicePixelRatio() : 1.0;
  params.cursors = &m_cursorSpans;
  params.indentGuideColor = m_theme->indentGuide();
  params.indentGuides = &m_guideSpans;
  params.activeIndentGuideColor = m_theme->indentGuideActive();
  params.activeIndentGuides = &m_activeGuideSpans;
  params.cursorVisible = m_cursorVisible && m_hasFocus;
  params.gutterWidth = m_gutterWidth;
  params.gutterBackground = m_theme->gutterBackground();
  params.gutter = &m_gutter;
  params.renderType = toNodeRenderType(renderType());
  scene->sync(params);
  m_sceneStats = scene->stats();
  return scene;
}
