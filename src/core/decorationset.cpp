#include "core/decorationset.h"

#include <algorithm>

namespace qce {

namespace {
constexpr qsizetype kMaxDrift = 4096;
} // namespace

DecorationSet::DecorationSet(TextDocument *document, QObject *parent) : QObject(parent), m_document(document) {
  connect(document, &TextDocument::changed, this, &DecorationSet::onChanged);
  // After a reset the lines the decorations referred to are gone.
  connect(document, &TextDocument::textReset, this, [this] {
    if (size() == 0)
      return;
    quint32 kinds = 0;
    for (int k = 0; k < kDecorationKindCount; ++k)
      if (m_counts[k] > 0)
        kinds |= 1u << k;
    Touched ignored;
    for (const Entry &entry : m_entries)
      destroy(entry, ignored);
    for (const Entry &entry : m_long)
      destroy(entry, ignored);
    m_entries.clear();
    m_long.clear();
    m_maxSpan = m_drift = 0;
    emit changed(0, qMax<qsizetype>(0, m_document->rope().lineCount() - 1), kinds);
  });
}

DecorationSet::~DecorationSet() {
  Touched ignored;
  for (const Entry &entry : m_entries)
    destroy(entry, ignored);
  for (const Entry &entry : m_long)
    destroy(entry, ignored);
}

qsizetype DecorationSet::endOf(const Entry &entry) const {
  const AnchorSet &anchors = m_document->anchors();
  // A fully deleted range collapses its right-leaning start past its left-leaning end.
  return qMax(anchors.offset(entry.start), anchors.offset(entry.end));
}

Decoration DecorationSet::toDecoration(const Entry &entry) const {
  Decoration d;
  d.id = entry.id;
  d.layer = entry.layer;
  d.start = startOf(entry);
  d.end = endOf(entry);
  d.kind = entry.kind;
  d.color = entry.color;
  d.text = entry.text;
  d.severity = entry.severity;
  d.priority = entry.priority;
  d.tag = entry.tag;
  d.icon = entry.icon;
  d.startGravity = m_document->anchors().gravity(entry.start);
  return d;
}

DecorationSet::Entry DecorationSet::makeEntry(const DecorationSpec &spec, int layer) {
  const qsizetype length = m_document->length();
  const qsizetype start = qBound<qsizetype>(0, spec.start, length);
  const qsizetype end = qBound(start, spec.end, length);
  AnchorSet &anchors = m_document->anchors();
  Entry entry;
  entry.id = m_nextId++;
  entry.layer = layer;
  entry.start = anchors.create(start, spec.startGravity);
  entry.end = anchors.create(end, spec.endGravity);
  entry.kind = spec.kind;
  entry.severity = spec.severity;
  entry.priority = spec.priority;
  entry.tag = spec.tag;
  entry.color = spec.color;
  entry.text = spec.text;
  entry.icon = spec.icon;
  ++m_counts[int(entry.kind)];
  if (spec.startGravity == Gravity::Left)
    ++m_leftStarts;
  return entry;
}

void DecorationSet::destroy(const Entry &entry, Touched &touched) {
  AnchorSet &anchors = m_document->anchors();
  touched.add(anchors.offset(entry.start), entry.kind);
  if (entry.kind == DecorationKind::InlineText)
    touched.inlineOffsets.append(anchors.offset(entry.start));
  if (anchors.gravity(entry.start) == Gravity::Left)
    --m_leftStarts;
  --m_counts[int(entry.kind)];
  anchors.remove(entry.start);
  anchors.remove(entry.end);
}

size_t DecorationSet::lowerBound(qsizetype offset) const {
  size_t lo = 0, hi = m_entries.size();
  while (lo < hi) {
    const size_t mid = (lo + hi) / 2;
    if (startOf(m_entries[mid]) < offset)
      lo = mid + 1;
    else
      hi = mid;
  }
  return lo;
}

void DecorationSet::insertEntry(Entry entry) {
  const qsizetype start = startOf(entry);
  const qsizetype span = endOf(entry) - start;
  if (span > kLongSpan) {
    m_long.push_back(std::move(entry));
    return;
  }
  // After equal start offsets: decorations added first stay first.
  size_t index = lowerBound(start);
  while (index < m_entries.size() && startOf(m_entries[index]) == start)
    ++index;
  m_maxSpan = qMax(m_maxSpan, span);
  m_entries.insert(m_entries.begin() + qsizetype(index), std::move(entry));
}

void DecorationSet::emitTouched(const Touched &touched) {
  if (touched.first < 0)
    return;
  const Rope &rope = m_document->rope();
  const qsizetype length = rope.length();
  if (!touched.inlineOffsets.isEmpty()) {
    QList<qsizetype> offsets = touched.inlineOffsets;
    std::sort(offsets.begin(), offsets.end());
    QList<qsizetype> lines;
    for (qsizetype offset : std::as_const(offsets)) {
      const qsizetype line = rope.lineAt(qMin(offset, length));
      if (lines.isEmpty() || lines.last() != line)
        lines.append(line);
    }
    emit inlineLinesChanged(lines);
  }
  emit changed(
    rope.lineAt(qMin(touched.first, length)), rope.lineAt(qMin(touched.last, length)), touched.kinds
  );
}

int DecorationSet::add(const DecorationSpec &spec, int layer) {
  Entry entry = makeEntry(spec, layer);
  const int id = entry.id;
  Touched touched;
  touched.add(startOf(entry), entry.kind);
  touched.add(endOf(entry), entry.kind);
  if (entry.kind == DecorationKind::InlineText)
    touched.inlineOffsets.append(startOf(entry));
  insertEntry(std::move(entry));
  emitTouched(touched);
  return id;
}

bool DecorationSet::remove(int id) {
  Touched touched;
  for (auto *list : {&m_entries, &m_long}) {
    for (size_t i = 0; i < list->size(); ++i) {
      if ((*list)[i].id != id)
        continue;
      touched.add(endOf((*list)[i]), (*list)[i].kind);
      destroy((*list)[i], touched);
      list->erase(list->begin() + qsizetype(i));
      emitTouched(touched);
      return true;
    }
  }
  return false;
}

void DecorationSet::setLayer(int layer, const QList<DecorationSpec> &specs) {
  Touched touched;
  // Drop the old contents of the layer.
  for (auto *list : {&m_entries, &m_long}) {
    auto kept = std::remove_if(list->begin(), list->end(), [&](const Entry &entry) {
      if (entry.layer != layer)
        return false;
      touched.add(endOf(entry), entry.kind);
      destroy(entry, touched);
      return true;
    });
    list->erase(kept, list->end());
  }

  // Build the new ones, sort them by start and merge them in as one run.
  const size_t oldSize = m_entries.size();
  m_entries.reserve(oldSize + size_t(specs.size()));
  for (const DecorationSpec &spec : specs) {
    Entry entry = makeEntry(spec, layer);
    touched.add(startOf(entry), entry.kind);
    touched.add(endOf(entry), entry.kind);
    if (entry.kind == DecorationKind::InlineText)
      touched.inlineOffsets.append(startOf(entry));
    const qsizetype span = endOf(entry) - startOf(entry);
    if (span > kLongSpan) {
      m_long.push_back(std::move(entry));
      continue;
    }
    m_maxSpan = qMax(m_maxSpan, span);
    m_entries.push_back(std::move(entry));
  }
  const AnchorSet &anchors = m_document->anchors();
  auto byStart = [&](const Entry &a, const Entry &b) { return anchors.offset(a.start) < anchors.offset(b.start); };
  std::stable_sort(m_entries.begin() + qsizetype(oldSize), m_entries.end(), byStart);
  std::inplace_merge(m_entries.begin(), m_entries.begin() + qsizetype(oldSize), m_entries.end(), byStart);
  emitTouched(touched);
}

void DecorationSet::clearLayer(int layer) { setLayer(layer, {}); }

void DecorationSet::clear() {
  Touched touched;
  for (const Entry &entry : m_entries) {
    touched.add(endOf(entry), entry.kind);
    destroy(entry, touched);
  }
  for (const Entry &entry : m_long) {
    touched.add(endOf(entry), entry.kind);
    destroy(entry, touched);
  }
  m_entries.clear();
  m_long.clear();
  m_maxSpan = m_drift = 0;
  emitTouched(touched);
}

qsizetype DecorationSet::layerSize(int layer) const {
  qsizetype n = 0;
  for (const Entry &entry : m_entries)
    n += entry.layer == layer;
  for (const Entry &entry : m_long)
    n += entry.layer == layer;
  return n;
}

bool DecorationSet::contains(int id) const { return decoration(id).id != 0; }

Decoration DecorationSet::decoration(int id) const {
  for (const auto *list : {&m_entries, &m_long})
    for (const Entry &entry : *list)
      if (entry.id == id)
        return toDecoration(entry);
  return {};
}

void DecorationSet::recomputeSpan() const {
  m_maxSpan = 0;
  for (const Entry &entry : m_entries)
    m_maxSpan = qMax(m_maxSpan, endOf(entry) - startOf(entry));
  m_drift = 0;
}

void DecorationSet::onChanged(const TextChange &change) {
  const qsizetype growth = change.newEnd - change.oldEnd;
  if (growth > 0)
    m_maxSpan += growth, m_drift += growth;
  if (m_leftStarts == 0 || m_entries.empty())
    return;
  // Starts that lean Left and starts that lean Right inside the replaced text end up on opposite
  // sides of the new text. Everything before the edit is unchanged and everything after it moved
  // as one, so only the entries whose start is in [start, newEnd] can be out of order.
  size_t lo = lowerBound(change.start), hi = lo;
  {
    size_t a = lo, b = m_entries.size();
    while (a < b) {
      const size_t mid = (a + b) / 2;
      if (startOf(m_entries[mid]) <= change.newEnd)
        a = mid + 1;
      else
        b = mid;
    }
    hi = a;
  }
  if (hi - lo < 2)
    return;
  const AnchorSet &anchors = m_document->anchors();
  std::stable_sort(m_entries.begin() + qsizetype(lo), m_entries.begin() + qsizetype(hi), [&](const Entry &a, const Entry &b) {
    return anchors.offset(a.start) < anchors.offset(b.start);
  });
}

QList<Decoration> DecorationSet::query(qsizetype firstOffset, qsizetype lastOffset, quint32 kinds) const {
  QList<Decoration> out;
  quint32 present = 0;
  for (int k = 0; k < kDecorationKindCount; ++k)
    if (m_counts[k] > 0)
      present |= 1u << k;
  if (!(present & kinds))
    return out;
  if (m_drift > kMaxDrift)
    recomputeSpan();
  for (size_t i = lowerBound(firstOffset - m_maxSpan); i < m_entries.size(); ++i) {
    const Entry &entry = m_entries[i];
    if (startOf(entry) > lastOffset)
      break;
    if ((decorationKindBit(entry.kind) & kinds) && endOf(entry) >= firstOffset)
      out.append(toDecoration(entry));
  }
  if (!m_long.empty()) {
    const qsizetype before = out.size();
    for (const Entry &entry : m_long)
      if ((decorationKindBit(entry.kind) & kinds) && startOf(entry) <= lastOffset && endOf(entry) >= firstOffset)
        out.append(toDecoration(entry));
    if (out.size() != before)
      std::stable_sort(out.begin(), out.end(), [](const Decoration &a, const Decoration &b) { return a.start < b.start; });
  }
  return out;
}

QList<Decoration> DecorationSet::queryLines(qsizetype firstLine, qsizetype lastLine, quint32 kinds) const {
  const Rope &rope = m_document->rope();
  const qsizetype last = rope.lineCount() - 1;
  firstLine = qBound<qsizetype>(0, firstLine, last);
  lastLine = qBound(firstLine, lastLine, last);
  return query(rope.lineStart(firstLine), rope.lineEnd(lastLine), kinds);
}

bool DecorationSet::validate() const {
  const AnchorSet &anchors = m_document->anchors();
  qsizetype counts[kDecorationKindCount] = {};
  qsizetype leftStarts = 0;
  qsizetype previous = -1;
  for (const auto *list : {&m_entries, &m_long}) {
    for (const Entry &entry : *list) {
      if (!anchors.contains(entry.start) || !anchors.contains(entry.end))
        return false;
      ++counts[int(entry.kind)];
      leftStarts += anchors.gravity(entry.start) == Gravity::Left;
      if (list == &m_entries) {
        const qsizetype start = anchors.offset(entry.start);
        if (start < previous)
          return false;
        previous = start;
      }
    }
  }
  for (int k = 0; k < kDecorationKindCount; ++k)
    if (counts[k] != m_counts[k])
      return false;
  return leftStarts == m_leftStarts;
}

} // namespace qce
