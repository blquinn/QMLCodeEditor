#include "codeeditor.h"

#include <QtGui/QTextOption>

#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGRectangleNode>
#include <cmath>

namespace {

// Everything the editor puts in the scene graph hangs off this node, so it is destroyed with the
// scene graph and never outlives the render thread's resources.
class EditorRoot : public QSGNode {
public:
  QSGRectangleNode *background = nullptr;
};

} // namespace

CodeEditor::CodeEditor(QQuickItem *parent) : QQuickItem(parent) {
  setFlag(ItemHasContents);
  setFlag(ItemIsFocusScope);
  m_font = qce::TextMetrics::defaultMonospaceFont();
  m_metrics.setFont(m_font);
  m_ownedTheme = m_theme = qce::Theme::createDark(this);
  connect(m_theme, &qce::Theme::changed, this, &CodeEditor::onThemeChanged);

  m_highlighter = m_nullHighlighter = new qce::NullHighlighter(this);

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
  polish();
}

void CodeEditor::invalidateLayouts() {
  m_layouts.clear();
  polish();
}

void CodeEditor::setContentY(qreal y) {
  y = qMax<qreal>(0, y);
  if (y == m_contentY)
    return;
  m_contentY = y;
  polish();
}

CodeEditor::RenderStats CodeEditor::renderStats() const {
  return {m_layouts.stats().created, m_layouts.stats().hits, m_layouts.size(), m_plan.size()};
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
  emit lineCountChanged();
  polish();
}

void CodeEditor::onDocumentChanged(const qce::TextChange &change) {
  const qsizetype first = change.startPos.line;
  m_layouts.invalidate(first, change.oldEndPos.line - first + 1, change.newEndPos.line - first + 1);
  if (const qsizetype count = lineCount(); count != m_lastLineCount) {
    m_lastLineCount = count;
    emit lineCountChanged();
  }
  polish();
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
  m_plan.clear();
  const qsizetype rowCount = m_map.rowCount();
  const qreal lineHeight = m_metrics.lineHeight();
  const qsizetype visibleRows = qsizetype(std::ceil(height() / lineHeight)) + 1;
  const qsizetype margin = qMax<qsizetype>(4, visibleRows / 2);
  const qsizetype top = qsizetype(std::floor(m_contentY / lineHeight));
  const qsizetype firstRow = qBound<qsizetype>(0, top - margin, rowCount - 1);
  const qsizetype lastRow = qBound<qsizetype>(0, top + visibleRows + margin, rowCount - 1);

  m_layouts.setCapacity(qMax<qsizetype>(256, 3 * (lastRow - firstRow + 1)));
  const qce::TextSnapshot snapshot = m_document.snapshot();
  m_plan.reserve(lastRow - firstRow + 1);
  for (qsizetype row = firstRow; row <= lastRow; ++row)
    m_plan.append({row, layoutForLine(m_map.rowAt(row).line, snapshot)});
  update();
}

void CodeEditor::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) {
  QQuickItem::geometryChange(newGeometry, oldGeometry);
  if (newGeometry.size() != oldGeometry.size())
    polish();
}

QSGNode *CodeEditor::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) {
  auto *root = static_cast<EditorRoot *>(oldNode);
  if (!root) {
    root = new EditorRoot;
    root->background = window()->createRectangleNode();
    root->appendChildNode(root->background);
  }
  root->background->setRect(boundingRect());
  root->background->setColor(m_theme->background());
  return root;
}
