#include "codeeditor.h"

#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>
#include <QtGui/QTextOption>

#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGRectangleNode>
#include <QtCore/QElapsedTimer>

#include <algorithm>
#include <cmath>

// Geometry for movement commands, from the same layouts and metrics the editor draws with.
class CodeEditor::EditorLayout final : public qce::CursorLayout {
public:
  explicit EditorLayout(CodeEditor *editor) : m_editor(editor) {}

  qreal xForOffset(qsizetype offset) const override {
    const qce::TextSnapshot snapshot = m_editor->m_document.snapshot();
    const qce::TextPosition pos = snapshot.rope().positionAt(offset);
    return m_editor->xForColumn(*m_editor->layoutForLine(pos.line, snapshot), pos.column);
  }
  qsizetype offsetForX(const qce::DisplayRow &row, qreal x) const override {
    const qce::TextSnapshot snapshot = m_editor->m_document.snapshot();
    const qsizetype column = qBound(
      row.startColumn, m_editor->columnForX(*m_editor->layoutForLine(row.line, snapshot), x), row.endColumn
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

private:
  CodeEditor *m_editor;
};

CodeEditor::CodeEditor(QQuickItem *parent) : QQuickItem(parent) {
  setFlag(ItemHasContents);
  setFlag(ItemIsFocusScope);
  m_cursorLayout = std::make_unique<EditorLayout>(this);
  m_host = std::make_unique<EditorHost>(this);
  m_font = qce::TextMetrics::defaultMonospaceFont();
  m_metrics.setFont(m_font);
  m_ownedTheme = m_theme = qce::Theme::createDark(this);
  connect(m_theme, &qce::Theme::changed, this, &CodeEditor::onThemeChanged);

  m_highlighter = m_nullHighlighter = new qce::NullHighlighter(this);

  connect(&m_selections, &qce::SelectionSet::changed, this, &CodeEditor::onSelectionsChanged);
  m_blinkTimer.setInterval(530);
  connect(&m_blinkTimer, &QTimer::timeout, this, [this] {
    m_cursorVisible = !m_cursorVisible;
    emit cursorVisibleChanged();
    update();
  });
  m_blinkTimer.start();

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
  });
  connect(&m_document, &qce::TextDocument::loadFailed, this, [this](const QString &error) {
    emit loadingChanged();
    emit loadFailed(error);
  });
}

CodeEditor::~CodeEditor() = default;

void CodeEditor::setFont(const QFont &font) {
  if (m_font == font)
    return;
  m_font = font;
  m_metrics.setFont(font);
  invalidateLayouts();
  updateContentSize();
  emit fontChanged();
}

void CodeEditor::setTabWidth(int columns) {
  columns = qBound(1, columns, 32);
  if (columns == m_metrics.tabWidth())
    return;
  m_metrics.setTabWidth(columns);
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
  m_highlighter = highlighter;
  connect(m_highlighter, &qce::Highlighter::invalidated, this, &CodeEditor::onHighlightInvalidated);
  onHighlightInvalidated(qce::Highlighter::AllLines, qce::Highlighter::AllLines);
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
qsizetype CodeEditor::selectionStart() const { return m_selections.primary().start(); }
qsizetype CodeEditor::selectionEnd() const { return m_selections.primary().end(); }

void CodeEditor::setCursorPosition(qsizetype offset) { select(offset, offset); }

void CodeEditor::select(qsizetype anchor, qsizetype head) {
  m_document.breakUndoCoalescing();
  m_selections.setSingle(anchor, head);
}

qce::EditContext CodeEditor::editContext() {
  return {
    m_document, m_selections, {true, m_metrics.tabWidth(), m_metrics.tabWidth(), m_readOnly}, &m_map,
    m_cursorLayout.get()
  };
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
  m_handler->reset();
  m_handler = handler;
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
  qce::EditContext ctx = editContext();
  if (m_handler->keyPress(event, ctx, *m_host)) {
    event->accept();
    afterCommand();
    return;
  }
  QQuickItem::keyPressEvent(event);
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

// Selected text of every selection, joined by line breaks.
void CodeEditor::copy() {
  const qce::Rope &rope = m_document.rope();
  QStringList parts;
  for (int i = 0; i < m_selections.count(); ++i)
    if (const qce::Selection s = m_selections.at(i); !s.isEmpty())
      parts.append(rope.toString(s.start(), s.end()));
  if (!parts.isEmpty())
    QGuiApplication::clipboard()->setText(parts.join(u'\n'));
}

void CodeEditor::cut() {
  if (m_readOnly)
    return;
  copy();
  qce::EditContext ctx = editContext();
  qce::commands::deleteSelection(ctx);
  afterCommand();
}

void CodeEditor::paste() {
  const QString text = QGuiApplication::clipboard()->text();
  if (text.isEmpty())
    return;
  qce::EditContext ctx = editContext();
  qce::commands::insertText(ctx, text, qce::EditKind::Other);
  afterCommand();
}

void CodeEditor::ensureCursorVisible() {
  const qce::TextSnapshot snapshot = m_document.snapshot();
  const qce::TextPosition pos = snapshot.rope().positionAt(cursorPosition());
  const auto layout = layoutForLine(pos.line, snapshot);
  if (layout->width > m_maxLineWidth) {
    m_maxLineWidth = layout->width;
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
  else if (x + margin + m_metrics.cellAdvance() > m_contentX + width())
    setContentX(x + margin + m_metrics.cellAdvance() - width());
}

// Selections moved: by a command, by select(), or because an edit shifted their anchors. The
// signal goes out only when the primary selection or the set really changed.
void CodeEditor::onSelectionsChanged() {
  const qce::Selection now = m_selections.primary();
  const bool same = now == m_lastSelection && m_selections.count() == m_lastSelectionCount;
  m_lastSelection = now;
  m_lastSelectionCount = m_selections.count();
  invalidatePlan();
  if (same)
    return;
  emit selectionChanged();
  restartBlink();
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
  if (m_blinkTimer.interval() > 0)
    m_blinkTimer.start();
  else
    m_blinkTimer.stop();
}

void CodeEditor::setContentX(qreal x) {
  x = qBound<qreal>(0, x, qMax<qreal>(0, m_contentWidth - width()));
  if (x == m_contentX)
    return;
  m_contentX = x;
  emit contentXChanged();
  invalidatePlan(); // the current-line highlight spans the visible columns
}

void CodeEditor::setContentY(qreal y) {
  y = qBound<qreal>(0, y, qMax<qreal>(0, m_contentHeight - height()));
  if (y == m_contentY)
    return;
  m_contentY = y;
  emit contentYChanged();
  polish(); // cheap when the layout window still covers the viewport
}

void CodeEditor::updateContentSize() {
  const qreal height = qreal(m_map.rowCount()) * m_metrics.lineHeight();
  if (height != m_contentHeight) {
    m_contentHeight = height;
    emit contentHeightChanged();
  }
  // One cell of slack so a cursor at the end of the widest line is reachable.
  const qreal width = m_maxLineWidth > 0 ? m_maxLineWidth + m_metrics.cellAdvance() : 0;
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

qreal CodeEditor::xForColumn(const qce::LineLayout &layout, qsizetype column) const {
  const QString &text = layout.text;
  return m_metrics.isSimple(text) ? m_metrics.xForColumn(text, column)
                                  : layout.layout->lineAt(0).cursorToX(int(column));
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
    const bool simple = m_metrics.isSimple(text);
    qsizetype cell = 0;
    for (qsizetype i = 0; i < text.size(); ++i) {
      const bool tab = text[i] == u'\t';
      const qsizetype nextCell = cell + (tab ? m_metrics.tabWidth() - cell % m_metrics.tabWidth() : 1);
      if (tab) {
        const qreal x0 = simple ? cell * m_metrics.cellAdvance() : layout.layout->lineAt(0).cursorToX(int(i));
        const qreal x1 =
          simple ? nextCell * m_metrics.cellAdvance() : layout.layout->lineAt(0).cursorToX(int(i) + 1);
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
  const qce::Rope &rope = m_document.rope();
  auto planLayout = [&](qsizetype row) -> const qce::LineLayout * {
    return row >= m_planFirst && row <= m_planLast ? m_plan[row - m_planFirst].layout.get() : nullptr;
  };
  const qreal cell = m_metrics.cellAdvance();

  // Selections are sorted, so rows outside the plan are skipped without looking at their text.
  const int primary = m_selections.primaryIndex();
  for (int i = 0; i < m_selections.count(); ++i) {
    const qce::Selection sel = m_selections.at(i);
    const qce::TextPosition head = rope.positionAt(sel.head);
    const qsizetype headRow = m_map.rowForPosition(head);
    if (const qce::LineLayout *layout = planLayout(headRow)) {
      const qreal x = xForColumn(*layout, head.column);
      m_cursorSpans.append({headRow, x, x + 2});
      if (i == primary && sel.isEmpty())
        m_currentLineSpans.append({headRow, m_contentX, m_contentX + width()});
    }
    if (sel.isEmpty())
      continue;
    const qce::TextPosition start = rope.positionAt(sel.start());
    const qce::TextPosition end = rope.positionAt(sel.end());
    const qsizetype startRow = m_map.rowForPosition(start);
    const qsizetype endRow = m_map.rowForPosition(end);
    for (qsizetype row = qMax(startRow, m_planFirst); row <= qMin(endRow, m_planLast); ++row) {
      const qce::LineLayout *layout = planLayout(row);
      const qreal x0 = row == startRow ? xForColumn(*layout, start.column) : 0;
      // Rows the selection continues past include their line break as one cell.
      const qreal x1 = row == endRow ? xForColumn(*layout, end.column) : layout->width + cell;
      if (x1 > x0)
        m_selectionSpans.append({row, x0, x1});
    }
  }
}

qsizetype CodeEditor::columnForX(const qce::LineLayout &layout, qreal x) const {
  return m_metrics.isSimple(layout.text)
           ? m_metrics.columnForX(layout.text, x)
           : layout.layout->lineAt(0).xToCursor(x, QTextLine::CursorBetweenCharacters);
}

qsizetype CodeEditor::positionAt(qreal x, qreal y) {
  const qsizetype row = qBound<qsizetype>(
    0, qsizetype(std::floor((y + m_contentY) / m_metrics.lineHeight())), m_map.rowCount() - 1
  );
  const qce::DisplayRow displayRow = m_map.rowAt(row);
  const qce::TextSnapshot snapshot = m_document.snapshot();
  const qce::Rope &rope = snapshot.rope();
  const auto layout = layoutForLine(displayRow.line, snapshot);
  const qsizetype column = columnForX(*layout, x + m_contentX);
  return rope.snapToCodePoint(rope.offsetAt({displayRow.line, column}));
}

QRectF CodeEditor::rectForPosition(qsizetype offset) {
  const qce::TextSnapshot snapshot = m_document.snapshot();
  const qce::TextPosition position = snapshot.rope().positionAt(offset);
  const qsizetype row = m_map.rowForPosition(position);
  const auto layout = layoutForLine(position.line, snapshot);
  const QString &text = layout->text;
  const qreal cursorX = m_metrics.isSimple(text) ? m_metrics.xForColumn(text, position.column)
                                                 : layout->layout->lineAt(0).cursorToX(int(position.column));
  return QRectF(
    cursorX - m_contentX, qreal(row) * m_metrics.lineHeight() - m_contentY, m_metrics.cellAdvance(),
    m_metrics.lineHeight()
  );
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

void CodeEditor::onDocumentReset() {
  updateUndoState();
  m_lastLineCount = lineCount();
  m_layouts.clear();
  m_maxLineWidth = 0;
  emit lineCountChanged();
  updateContentSize();
  invalidatePlan();
}

void CodeEditor::onDocumentChanged(const qce::TextChange &change) {
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

} // namespace

std::shared_ptr<qce::LineLayout>
CodeEditor::layoutForLine(qsizetype line, const qce::TextSnapshot &snapshot) {
  if (auto cached = m_layouts.find(line))
    return cached;

  const qce::Rope &rope = snapshot.rope();
  const QString text = rope.toString(rope.lineStart(line), rope.lineEnd(line));
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
  const auto spans = m_highlighter->highlightLines(snapshot, line, line);
  QList<QTextLayout::FormatRange> formats;
  if (!spans.isEmpty())
    formats = m_theme->formatRanges(spans.first());
  if (m_showWhitespace)
    formats = withWhitespaceFormats(text, std::move(formats), m_theme->whitespace());
  if (!formats.isEmpty())
    layout->setFormats(formats);
  layout->beginLayout();
  QTextLine textLine = layout->createLine();
  textLine.setLineWidth(1e9);
  textLine.setPosition(QPointF(0, 0));
  const qreal width = textLine.naturalTextWidth();
  layout->endLayout();
  return m_layouts.insert(line, std::move(layout), width, text);
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
  const qsizetype rowCount = m_map.rowCount();
  const qreal lineHeight = m_metrics.lineHeight();
  const qsizetype visibleRows = qsizetype(std::ceil(height() / lineHeight)) + 1;
  const qsizetype margin = qMax<qsizetype>(4, visibleRows / 2);
  const qsizetype top = qsizetype(std::floor(m_contentY / lineHeight));
  const qsizetype firstRow = qBound<qsizetype>(0, top - margin, rowCount - 1);
  const qsizetype lastRow = qBound<qsizetype>(0, top + visibleRows + margin, rowCount - 1);

  // Plain scrolling inside the layout window changes nothing but the scroll transform.
  if (!m_planDirty && firstRow == m_planFirst && lastRow == m_planLast) {
    update();
    return;
  }

  m_plan.clear();
  m_layouts.setCapacity(qMax<qsizetype>(256, 3 * (lastRow - firstRow + 1)));
  const qce::TextSnapshot snapshot = m_document.snapshot();
  m_plan.reserve(lastRow - firstRow + 1);
  qreal widest = m_maxLineWidth;
  for (qsizetype row = firstRow; row <= lastRow; ++row) {
    auto layout = layoutForLine(m_map.rowAt(row).line, snapshot);
    widest = qMax(widest, layout->width);
    m_plan.append({row, std::move(layout)});
  }
  m_planFirst = firstRow;
  m_planLast = lastRow;
  m_planDirty = false;
  buildOverlays();
  m_maxLineWidth = widest;
  updateContentSize();
  update();
}

void CodeEditor::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) {
  QQuickItem::geometryChange(newGeometry, oldGeometry);
  if (newGeometry.size() != oldGeometry.size()) {
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
  params.currentLine = &m_currentLineSpans;
  params.selection = &m_selectionSpans;
  params.markColor = m_theme->whitespace();
  params.marks = &m_markSpans;
  params.cursors = &m_cursorSpans;
  params.cursorVisible = m_cursorVisible;
  scene->sync(params);
  m_sceneStats = scene->stats();
  return scene;
}
