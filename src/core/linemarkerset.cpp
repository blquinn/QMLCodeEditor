#include "core/linemarkerset.h"

#include <algorithm>

namespace qce {

namespace {
constexpr qsizetype kMaxDrift = 4096;
}

LineMarkerSet::LineMarkerSet(TextDocument *document, QObject *parent) : QObject(parent), m_document(document) {
  connect(document, &TextDocument::changed, this, &LineMarkerSet::onChanged);
}

LineMarkerSet::~LineMarkerSet() {
  clear();
}

LineMarkerSet::Marker LineMarkerSet::toMarker(const Entry &entry) const {
  const Rope &rope = m_document->rope();
  const AnchorSet &anchors = m_document->anchors();
  // An insertion at the start of an empty marked line moves the start past the end; the marker stays
  // on the line it was pushed to.
  const qsizetype start = anchors.offset(entry.start);
  const qsizetype end = qMax(start, anchors.offset(entry.end));
  return {entry.id, rope.lineAt(start), rope.lineAt(end), entry.kind, entry.priority};
}

size_t LineMarkerSet::lowerBound(qsizetype offset) const {
  const AnchorSet &anchors = m_document->anchors();
  size_t lo = 0, hi = m_entries.size();
  while (lo < hi) {
    const size_t mid = (lo + hi) / 2;
    if (anchors.offset(m_entries[mid].start) < offset)
      lo = mid + 1;
    else
      hi = mid;
  }
  return lo;
}

int LineMarkerSet::add(qsizetype firstLine, qsizetype lastLine, int kind, int priority) {
  const Rope &rope = m_document->rope();
  const qsizetype last = rope.lineCount() - 1;
  firstLine = qBound<qsizetype>(0, firstLine, last);
  lastLine = qBound(firstLine, lastLine, last);
  AnchorSet &anchors = m_document->anchors();
  const qsizetype startOffset = rope.lineStart(firstLine);
  const qsizetype endOffset = rope.lineEnd(lastLine);
  Entry entry{m_nextId++, anchors.create(startOffset, Gravity::Right), anchors.create(endOffset, Gravity::Left), kind,
              priority};
  // After equal start offsets: markers added first stay first.
  size_t index = lowerBound(startOffset);
  while (index < m_entries.size() && anchors.offset(m_entries[index].start) == startOffset)
    ++index;
  m_entries.insert(m_entries.begin() + qsizetype(index), entry);
  m_maxSpan = qMax(m_maxSpan, endOffset - startOffset);
  emit changed();
  return entry.id;
}

void LineMarkerSet::eraseAt(size_t index) {
  AnchorSet &anchors = m_document->anchors();
  anchors.remove(m_entries[index].start);
  anchors.remove(m_entries[index].end);
  m_entries.erase(m_entries.begin() + qsizetype(index));
}

bool LineMarkerSet::remove(int id) {
  for (size_t i = 0; i < m_entries.size(); ++i)
    if (m_entries[i].id == id) {
      eraseAt(i);
      emit changed();
      return true;
    }
  return false;
}

void LineMarkerSet::clear() {
  if (m_entries.empty())
    return;
  AnchorSet &anchors = m_document->anchors();
  for (const Entry &entry : m_entries) {
    anchors.remove(entry.start);
    anchors.remove(entry.end);
  }
  m_entries.clear();
  m_maxSpan = m_drift = 0;
  emit changed();
}

bool LineMarkerSet::contains(int id) const {
  return std::any_of(m_entries.begin(), m_entries.end(), [id](const Entry &e) { return e.id == id; });
}

LineMarkerSet::Marker LineMarkerSet::marker(int id) const {
  for (const Entry &entry : m_entries)
    if (entry.id == id)
      return toMarker(entry);
  return {};
}

void LineMarkerSet::recomputeSpan() const {
  const AnchorSet &anchors = m_document->anchors();
  m_maxSpan = 0;
  for (const Entry &entry : m_entries)
    m_maxSpan = qMax(m_maxSpan, anchors.offset(entry.end) - anchors.offset(entry.start));
  m_drift = 0;
}

void LineMarkerSet::onChanged(const TextChange &change) {
  const qsizetype growth = (change.newEnd - change.oldEnd);
  if (growth > 0)
    m_maxSpan += growth, m_drift += growth;
  if (!m_entries.empty())
    emit changed();
}

QList<LineMarkerSet::Marker> LineMarkerSet::query(qsizetype firstLine, qsizetype lastLine) const {
  QList<Marker> out;
  if (m_entries.empty())
    return out;
  if (m_drift > kMaxDrift)
    recomputeSpan();
  const Rope &rope = m_document->rope();
  const AnchorSet &anchors = m_document->anchors();
  const qsizetype lineLast = rope.lineCount() - 1;
  firstLine = qBound<qsizetype>(0, firstLine, lineLast);
  lastLine = qBound(firstLine, lastLine, lineLast);
  const qsizetype lo = rope.lineStart(firstLine);
  const qsizetype hi = rope.lineEnd(lastLine);
  for (size_t i = lowerBound(lo - m_maxSpan); i < m_entries.size(); ++i) {
    const Entry &entry = m_entries[i];
    if (anchors.offset(entry.start) > hi)
      break;
    if (qMax(anchors.offset(entry.end), anchors.offset(entry.start)) >= lo)
      out.append(toMarker(entry));
  }
  return out;
}

void LineMarkerSet::markRange(qsizetype firstLine, qsizetype lastLine, int kind, int priority) {
  const Rope &rope = m_document->rope();
  const qsizetype last = rope.lineCount() - 1;
  firstLine = qBound<qsizetype>(0, firstLine, last);
  lastLine = qBound(firstLine, lastLine, last);
  // Touching counts: look one line either side.
  const QList<Marker> near = query(qMax<qsizetype>(0, firstLine - 1), qMin(last, lastLine + 1));
  qsizetype from = firstLine, to = lastLine;
  QList<int> merged;
  for (const Marker &m : near)
    if (m.kind == kind) {
      from = qMin(from, m.firstLine);
      to = qMax(to, m.lastLine);
      merged.append(m.id);
    }
  if (merged.size() == 1) {
    const Marker m = marker(merged.first());
    if (m.firstLine == from && m.lastLine == to)
      return; // already covered
  }
  for (int id : std::as_const(merged)) {
    for (size_t i = 0; i < m_entries.size(); ++i)
      if (m_entries[i].id == id) {
        eraseAt(i);
        break;
      }
  }
  add(from, to, kind, priority);
}

} // namespace qce
