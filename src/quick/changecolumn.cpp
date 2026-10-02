#include "quick/changecolumn.h"

#include "quick/codeeditor.h"

namespace qce {

ChangeColumn::ChangeColumn(QObject *parent) : GutterColumn(parent) {}
ChangeColumn::~ChangeColumn() = default;

void ChangeColumn::attach(CodeEditor *editor) {
  m_document = editor->document();
  m_set = std::make_unique<LineMarkerSet>(m_document);
  connect(m_set.get(), &LineMarkerSet::changed, this, &GutterColumn::contentChanged);
  // The marker set connected first, so its anchors have settled when this sees the change.
  connect(m_document, &TextDocument::changed, this, &ChangeColumn::onChanged);
  connect(m_document, &TextDocument::textReset, this, &ChangeColumn::reset);
  connect(m_document, &TextDocument::loadFinished, this, &ChangeColumn::reset);
  connect(editor, &CodeEditor::saved, this, &ChangeColumn::reset);
}

void ChangeColumn::detach(CodeEditor *editor) {
  disconnect(m_document, nullptr, this, nullptr);
  disconnect(editor, &CodeEditor::saved, this, &ChangeColumn::reset);
  m_set.reset();
  m_document = nullptr;
}

void ChangeColumn::reset() {
  if (m_set)
    m_set->clear();
}

bool ChangeColumn::isChanged(qsizetype line) const {
  return m_set && !m_set->query(line, line).isEmpty();
}

void ChangeColumn::onChanged(const TextChange &change) {
  // A file being read in is not an edit.
  if (m_document->isLoading())
    return;
  const bool removedWholeLines = change.insertedLength() == 0 && change.startPos.column == 0 &&
                                 change.oldEndPos.column == 0 && change.oldEndPos.line > change.startPos.line;
  if (removedWholeLines)
    m_set->markRange(change.startPos.line, change.startPos.line, Deleted);
  else
    m_set->markRange(change.startPos.line, change.newEndPos.line, Modified);
}

void ChangeColumn::paintRows(const GutterContext &context, const QList<FramePlanRow> &rows, GutterPainter &painter) {
  if (!m_set || rows.isEmpty() || m_set->size() == 0)
    return;
  const QList<LineMarkerSet::Marker> found = m_set->query(rows.first().display.line, rows.last().display.line);
  const qreal barWidth = qMin(painter.width(), 3.0);
  for (const FramePlanRow &row : rows) {
    const qsizetype line = row.display.line;
    for (const auto &marker : found) {
      if (line < marker.firstLine || line > marker.lastLine)
        continue;
      if (marker.kind == Modified) {
        painter.rect(row.row, 0, barWidth, context.theme->changeModified());
      } else if (row.display.isFirst()) {
        // Where the removed lines were: a short bar across the top edge of the line that followed them.
        painter.rect(row.row, 0, painter.width(), context.theme->changeDeleted(), 0, 2);
      }
    }
  }
}

} // namespace qce
