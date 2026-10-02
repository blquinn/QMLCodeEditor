#include "codeeditor.h"

#include <QtGui/QTextOption>

#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGRectangleNode>
#include <cmath>

CodeEditor::CodeEditor(QQuickItem *parent) : QQuickItem(parent) {
  setFlag(ItemHasContents);
  setFlag(ItemIsFocusScope);
  m_font = qce::TextMetrics::defaultMonospaceFont();
  m_metrics.setFont(m_font);
  m_ownedTheme = m_theme = qce::Theme::createDark(this);
  connect(m_theme, &qce::Theme::changed, this, &CodeEditor::onThemeChanged);

  m_highlighter = m_nullHighlighter = new qce::NullHighlighter(this);

  m_headAnchor = m_document.anchors().create(0, qce::Gravity::Right);
  m_selectionAnchor = m_document.anchors().create(0, qce::Gravity::Right);
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

qsizetype CodeEditor::cursorPosition() const { return m_document.anchors().offset(m_headAnchor); }
qsizetype CodeEditor::selectionStart() const {
  return qMin(cursorPosition(), m_document.anchors().offset(m_selectionAnchor));
}
qsizetype CodeEditor::selectionEnd() const {
  return qMax(cursorPosition(), m_document.anchors().offset(m_selectionAnchor));
}

void CodeEditor::setCursorPosition(qsizetype offset) { select(offset, offset); }

void CodeEditor::select(qsizetype anchor, qsizetype head) {
  const qce::Rope &rope = m_document.rope();
  anchor = rope.snapToCodePoint(qBound<qsizetype>(0, anchor, rope.length()));
  head = rope.snapToCodePoint(qBound<qsizetype>(0, head, rope.length()));
  if (anchor == m_document.anchors().offset(m_selectionAnchor) && head == cursorPosition())
    return;
  // Anchors can't be moved, so replace them.
  m_document.anchors().remove(m_selectionAnchor);
  m_document.anchors().remove(m_headAnchor);
  m_selectionAnchor = m_document.anchors().create(anchor, qce::Gravity::Right);
  m_headAnchor = m_document.anchors().create(head, qce::Gravity::Right);
  m_lastSelection = {anchor, head};
  emit selectionChanged();
  restartBlink();
  invalidatePlan();
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
  const QString &text = layout.layout->text();
  return m_metrics.isSimple(text) ? m_metrics.xForColumn(text, column)
                                  : layout.layout->lineAt(0).cursorToX(int(column));
}

// Cursor, selection and current-line spans for the rows in the plan. A selection covering the
// whole of a huge document still only produces spans for the rows being drawn.
void CodeEditor::buildOverlays() {
  m_currentLineSpans.clear();
  m_selectionSpans.clear();
  m_hasCursor = false;
  const qce::Rope &rope = m_document.rope();
  auto planLayout = [&](qsizetype row) -> const qce::LineLayout * {
    return row >= m_planFirst && row <= m_planLast ? m_plan[row - m_planFirst].layout.get() : nullptr;
  };
  const qreal cell = m_metrics.cellAdvance();

  const qce::TextPosition head = rope.positionAt(cursorPosition());
  const qsizetype headRow = m_map.rowForPosition(head);
  if (const qce::LineLayout *layout = planLayout(headRow)) {
    const qreal x = xForColumn(*layout, head.column);
    m_cursorSpan = {headRow, x, x + 2};
    m_hasCursor = true;
  }

  if (selectionStart() == selectionEnd()) {
    if (planLayout(headRow))
      m_currentLineSpans.append({headRow, m_contentX, m_contentX + width()});
    return;
  }
  const qce::TextPosition start = rope.positionAt(selectionStart());
  const qce::TextPosition end = rope.positionAt(selectionEnd());
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

qsizetype CodeEditor::positionAt(qreal x, qreal y) {
  const qsizetype row = qBound<qsizetype>(
    0, qsizetype(std::floor((y + m_contentY) / m_metrics.lineHeight())), m_map.rowCount() - 1
  );
  const qce::DisplayRow displayRow = m_map.rowAt(row);
  const qce::TextSnapshot snapshot = m_document.snapshot();
  const qce::Rope &rope = snapshot.rope();
  const auto layout = layoutForLine(displayRow.line, snapshot);
  const QString &text = layout->layout->text();
  const qreal contentX = x + m_contentX;
  const qsizetype column =
    m_metrics.isSimple(text)
      ? m_metrics.columnForX(text, contentX)
      : layout->layout->lineAt(0).xToCursor(contentX, QTextLine::CursorBetweenCharacters);
  return rope.snapToCodePoint(rope.offsetAt({displayRow.line, column}));
}

QRectF CodeEditor::rectForPosition(qsizetype offset) {
  const qce::TextSnapshot snapshot = m_document.snapshot();
  const qce::TextPosition position = snapshot.rope().positionAt(offset);
  const qsizetype row = m_map.rowForPosition(position);
  const auto layout = layoutForLine(position.line, snapshot);
  const QString &text = layout->layout->text();
  const qreal cursorX = m_metrics.isSimple(text) ? m_metrics.xForColumn(text, position.column)
                                                 : layout->layout->lineAt(0).cursorToX(int(position.column));
  return QRectF(
    cursorX - m_contentX, qreal(row) * m_metrics.lineHeight() - m_contentY, m_metrics.cellAdvance(),
    m_metrics.lineHeight()
  );
}

CodeEditor::RenderStats CodeEditor::renderStats() const {
  return {m_layouts.stats().created, m_layouts.stats().hits, m_layouts.size(), m_plan.size(), m_sceneStats};
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
  // Edits move the anchors; tell listeners when the cursor or selection ended up elsewhere.
  const qce::Selection now{m_document.anchors().offset(m_selectionAnchor), cursorPosition()};
  if (now != m_lastSelection) {
    m_lastSelection = now;
    emit selectionChanged();
    restartBlink();
  }
}

std::shared_ptr<qce::LineLayout>
CodeEditor::layoutForLine(qsizetype line, const qce::TextSnapshot &snapshot) {
  if (auto cached = m_layouts.find(line))
    return cached;

  const qce::Rope &rope = snapshot.rope();
  const QString text = rope.toString(rope.lineStart(line), rope.lineEnd(line));
  auto layout = std::make_unique<QTextLayout>(text, m_metrics.layoutFont());
  QTextOption option;
  option.setWrapMode(QTextOption::NoWrap);
  option.setTabStopDistance(m_metrics.tabWidth() * m_metrics.cellAdvance());
  layout->setTextOption(option);
  layout->setCacheEnabled(true);
  const auto spans = m_highlighter->highlightLines(snapshot, line, line);
  if (!spans.isEmpty())
    layout->setFormats(m_theme->formatRanges(spans.first()));
  layout->beginLayout();
  QTextLine textLine = layout->createLine();
  textLine.setLineWidth(1e9);
  textLine.setPosition(QPointF(0, 0));
  const qreal width = textLine.naturalTextWidth();
  layout->endLayout();
  return m_layouts.insert(line, std::move(layout), width);
}

// Lays out the viewport plus a margin of rows on each side. Nothing outside that window is touched,
// however large the document is.
void CodeEditor::updatePolish() {
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
  params.hasCursor = m_hasCursor;
  params.cursor = m_cursorSpan;
  params.cursorVisible = m_cursorVisible;
  scene->sync(params);
  m_sceneStats = scene->stats();
  return scene;
}
