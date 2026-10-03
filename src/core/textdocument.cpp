#include "core/textdocument.h"

#include "core/fileloader.h"
#include "core/filesaver.h"

namespace qce {

TextDocument::TextDocument(QObject *parent) : QObject(parent) {}
TextDocument::~TextDocument() { cancelLoad(); }

bool TextDocument::insert(qsizetype offset, QStringView text, const EditOptions &options) {
  return replace(offset, offset, text, options);
}

bool TextDocument::remove(qsizetype start, qsizetype end, const EditOptions &options) {
  return replace(start, end, QStringView(), options);
}

bool TextDocument::replace(qsizetype start, qsizetype end, QStringView text, const EditOptions &options) {
  if (m_loading)
    return false;
  return apply(start, end, Rope::fromString(text), &options);
}

bool TextDocument::replace(qsizetype start, qsizetype end, const Rope &text, const EditOptions &options) {
  if (m_loading)
    return false;
  return apply(start, end, text, &options);
}

void TextDocument::beginEditGroup(const SelectionList &before) { m_undo.beginGroup(before); }

void TextDocument::endEditGroup(const SelectionList &after, EditKind kind) { m_undo.endGroup(after, kind); }

std::optional<SelectionList> TextDocument::undo() {
  if (!canUndo())
    return std::nullopt;
  const Transaction t = m_undo.undo();
  for (qsizetype i = t.edits.size() - 1; i >= 0; --i) {
    const EditRecord &e = t.edits[i];
    apply(e.start, e.start + e.inserted.length(), e.removed, nullptr);
  }
  return t.selectionsBefore;
}

std::optional<SelectionList> TextDocument::redo() {
  if (!canRedo())
    return std::nullopt;
  const Transaction t = m_undo.redo();
  for (const EditRecord &e : t.edits)
    apply(e.start, e.start + e.removed.length(), e.inserted, nullptr);
  return t.selectionsAfter;
}

namespace {

// Edit offsets never fall inside a surrogate pair or a CRLF break: positions cannot express the
// middle of a CRLF (columns exclude the CR), so the pair is treated as one unit (ADR 0008).
qsizetype snapForEdit(const Rope &rope, qsizetype offset, Rope::Snap dir) {
  offset = rope.snapToCodePoint(offset, dir);
  if (
    offset > 0 && offset < rope.length() && rope.at(offset - 1) == QLatin1Char('\r') &&
    rope.at(offset) == QLatin1Char('\n')
  )
    return dir == Rope::Snap::Backward ? offset - 1 : offset + 1;
  return offset;
}

} // namespace

bool TextDocument::apply(
  qsizetype start, qsizetype end, const Rope &insertedText, const EditOptions *record
) {
  Rope text = insertedText;
  const bool pureInsert = start == end;
  start = snapForEdit(m_rope, qBound<qsizetype>(0, start, length()), Rope::Snap::Backward);
  end = pureInsert ? start : snapForEdit(m_rope, qBound(start, end, length()), Rope::Snap::Forward);
  if (start == end && text.isEmpty())
    return true;

  // The new text must not leave a boundary of the edit inside a CRLF either, or newEndPos could
  // not name it: widen the edit until both ends sit outside any break (ADR 0008).
  for (bool widened = true; widened;) {
    widened = false;
    const QChar before = start > 0 ? m_rope.at(start - 1) : QChar();
    const QChar first = !text.isEmpty() ? text.at(0) : (end < length() ? m_rope.at(end) : QChar());
    if (before == QLatin1Char('\r') && first == QLatin1Char('\n')) {
      --start;
      text = Rope::fromString(u"\r").concat(text);
      widened = true;
    }
    const QChar last =
      !text.isEmpty() ? text.at(text.length() - 1) : (start > 0 ? m_rope.at(start - 1) : QChar());
    if (last == QLatin1Char('\r') && end < length() && m_rope.at(end) == QLatin1Char('\n')) {
      ++end;
      text = text.concat(Rope::fromString(u"\n"));
      widened = true;
    }
  }

  TextChange change;
  change.start = start;
  change.oldEnd = end;
  change.newEnd = start + text.length();
  change.startPos = m_rope.positionAt(start);
  change.oldEndPos = m_rope.positionAt(end);
  change.removed = m_rope.slice(start, end);
  change.inserted = text;
  change.versionBefore = m_version;

  m_rope = m_rope.remove(start, end).insert(start, text);
  m_anchors.applyEdit(start, end, change.newEnd);
  if (record) {
    const EditRecord edit{start, change.removed, change.inserted};
    if (m_undo.inGroup())
      m_undo.addToGroup(edit);
    else
      m_undo.pushEdit(edit, record->kind, record->selectionsBefore, record->selectionsAfter);
  }
  change.versionAfter = ++m_version;
  change.newEndPos = m_rope.positionAt(change.newEnd);
  emit changed(change);
  return true;
}

void TextDocument::setText(QStringView text) {
  cancelLoad();
  reset(Rope::fromString(text));
}

void TextDocument::load(const QString &path) {
  cancelLoad();
  m_format = FileFormat();
  reset(Rope());
  m_loading = true;
  m_job = new FileLoadJob(path, this);
  connect(m_job, &FileLoadJob::progress, this, [this](const Rope &text, qint64 done, qint64 total) {
    appendLoaded(text, false);
    emit loadProgress(done, total);
  });
  connect(m_job, &FileLoadJob::finished, this, [this](const Rope &text, const FileFormat &format) {
    appendLoaded(text, true);
    m_format = format;
    m_loading = false;
    m_job->deleteLater();
    m_job = nullptr;
    emit loadFinished(format);
  });
  connect(m_job, &FileLoadJob::failed, this, [this](const QString &error) {
    m_loading = false;
    m_job->deleteLater();
    m_job = nullptr;
    emit loadFailed(error);
  });
  connect(m_job, &FileLoadJob::canceled, this, [this] {
    m_loading = false;
    m_job->deleteLater();
    m_job = nullptr;
  });
  m_job->start();
}

bool TextDocument::save(const QString &path, QString *error) const {
  if (m_loading) {
    if (error)
      *error = QStringLiteral("the document is still loading");
    return false;
  }
  return saveFile(m_rope, path, m_format, error);
}

void TextDocument::cancelLoad() {
  if (!m_job)
    return;
  delete m_job; // cancels and waits for the worker; pending signals are dropped
  m_job = nullptr;
  m_loading = false;
}

void TextDocument::appendLoaded(const Rope &full, bool final) {
  qsizetype length = full.length();
  // Don't publish half of a surrogate pair that the next slice will complete.
  if (!final && length > 0 && full.at(length - 1).isHighSurrogate())
    --length;
  const qsizetype old = m_rope.length();
  if (length > old)
    apply(old, old, full.slice(old, length), nullptr);
}

void TextDocument::reset(const Rope &rope) {
  m_anchors.applyEdit(0, m_rope.length(), rope.length());
  m_undo.clear();
  m_rope = rope;
  ++m_version;
  emit textReset();
}

} // namespace qce
