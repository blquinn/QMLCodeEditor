#include "core/textdocument.h"

namespace qce {

TextDocument::TextDocument(QObject *parent) : QObject(parent) {}
TextDocument::~TextDocument() = default;

bool TextDocument::insert(qsizetype offset, QStringView text) { return replace(offset, offset, text); }

bool TextDocument::remove(qsizetype start, qsizetype end) { return replace(start, end, QStringView()); }

bool TextDocument::replace(qsizetype start, qsizetype end, QStringView text) {
  return apply(start, end, Rope::fromString(text));
}

bool TextDocument::replace(qsizetype start, qsizetype end, const Rope &text) {
  return apply(start, end, text);
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

bool TextDocument::apply(qsizetype start, qsizetype end, const Rope &insertedText) {
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
  change.versionAfter = ++m_version;
  change.newEndPos = m_rope.positionAt(change.newEnd);
  emit changed(change);
  return true;
}

void TextDocument::setText(QStringView text) { reset(Rope::fromString(text)); }

void TextDocument::reset(const Rope &rope) {
  m_anchors.applyEdit(0, m_rope.length(), rope.length());
  m_rope = rope;
  ++m_version;
  emit textReset();
}

} // namespace qce
