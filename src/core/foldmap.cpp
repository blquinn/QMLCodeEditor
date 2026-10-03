#include "core/foldmap.h"

#include <algorithm>

namespace qce {

namespace {

// Rope::lineEnd for many lines at once, in one pass over the text: `lines` must ascend (repeats are
// fine). Each lookup on its own scans a leaf, which adds up when folding hundreds of thousands of ranges.
QList<qsizetype> lineEndOffsets(const Rope &rope, const QList<qsizetype> &lines) {
  QList<qsizetype> out(lines.size(), rope.length());
  qsizetype k = 0, line = 0, pos = 0;
  QChar previous;
  ChunkIterator it(rope);
  QStringView chunk;
  while (k < lines.size() && it.next(&chunk)) {
    qsizetype i = 0;
    while (k < lines.size()) {
      const qsizetype nl = chunk.indexOf(u'\n', i);
      if (nl < 0)
        break;
      const bool cr = (nl > 0 ? chunk[nl - 1] : previous) == u'\r';
      const qsizetype end = pos + nl - (cr ? 1 : 0);
      while (k < lines.size() && lines[k] <= line)
        out[k++] = end;
      ++line;
      i = nl + 1;
    }
    if (!chunk.isEmpty())
      previous = chunk.last();
    pos += chunk.size();
  }
  return out;
}

} // namespace

FoldMap::FoldMap(TextDocument *document) : m_document(document) {}

FoldMap::~FoldMap() {
  for (const Fold &fold : m_folds)
    dropAnchors(fold);
}

void FoldMap::dropAnchors(const Fold &fold) {
  m_document->anchors().remove(fold.start);
  m_document->anchors().remove(fold.end);
}

qsizetype FoldMap::stretchAtOrBefore(qsizetype line) const {
  const auto it = std::upper_bound(
    m_hidden.begin(), m_hidden.end(), line, [](qsizetype l, const Hidden &h) { return l < h.first; }
  );
  return qsizetype(it - m_hidden.begin()) - 1;
}

bool FoldMap::isHidden(qsizetype line) const {
  const qsizetype i = stretchAtOrBefore(line);
  return i >= 0 && line <= m_hidden[size_t(i)].last;
}

qsizetype FoldMap::visibleHeaderOf(qsizetype line) const {
  const qsizetype i = stretchAtOrBefore(line);
  return i >= 0 && line <= m_hidden[size_t(i)].last ? m_hidden[size_t(i)].first - 1 : line;
}

qsizetype FoldMap::nextVisibleLine(qsizetype line) const {
  const qsizetype i = stretchAtOrBefore(line + 1);
  if (i >= 0 && line + 1 <= m_hidden[size_t(i)].last)
    return m_hidden[size_t(i)].last + 1;
  return line + 1;
}

qsizetype FoldMap::foldLineForBufferLine(qsizetype line) const {
  return unclampedFoldLine(qBound<qsizetype>(0, line, bufferLineCount() - 1));
}

qsizetype FoldMap::unclampedFoldLine(qsizetype line) const {
  const qsizetype i = stretchAtOrBefore(line);
  if (i < 0)
    return line;
  const Hidden &h = m_hidden[size_t(i)];
  if (line <= h.last)
    return h.first - 1 - h.before;
  return line - h.before - h.count();
}

qsizetype FoldMap::bufferLineForFoldLine(qsizetype foldLine) const {
  foldLine = qBound<qsizetype>(0, foldLine, lineCount() - 1);
  // Stretches that lie wholly above the fold line: those with `first - before` (visible lines above
  // them) <= foldLine.
  const auto it = std::upper_bound(m_hidden.begin(), m_hidden.end(), foldLine, [](qsizetype f, const Hidden &h) {
    return f < h.first - h.before;
  });
  if (it == m_hidden.begin())
    return foldLine;
  const Hidden &h = *(it - 1);
  return foldLine + h.before + h.count();
}

qsizetype FoldMap::visibleLinesIn(qsizetype first, qsizetype last) const {
  if (last < first)
    return 0;
  const qsizetype a = unclampedFoldLine(qMax<qsizetype>(0, first)), b = unclampedFoldLine(last);
  // A hidden endpoint maps to its header, which is also counted when it is inside the range.
  qsizetype n = b - a + 1;
  if (isHidden(first))
    --n;
  return qMax<qsizetype>(0, n);
}

std::vector<FoldMap::Fold>::const_iterator FoldMap::findHeader(qsizetype header) const {
  return std::lower_bound(
    m_folds.begin(), m_folds.end(), header, [](const Fold &f, qsizetype h) { return f.header < h; }
  );
}

std::optional<FoldRange> FoldMap::foldAtHeader(qsizetype header) const {
  const auto it = findHeader(header);
  if (it == m_folds.end() || it->header != header)
    return std::nullopt;
  return FoldRange{it->header, it->last};
}

QList<FoldRange> FoldMap::folds() const {
  QList<FoldRange> out;
  out.reserve(qsizetype(m_folds.size()));
  for (const Fold &f : m_folds)
    out.append({f.header, f.last});
  return out;
}

QList<FoldRange> FoldMap::foldsWithHeaderIn(qsizetype first, qsizetype last) const {
  QList<FoldRange> out;
  for (auto it = findHeader(first); it != m_folds.end() && it->header <= last; ++it)
    out.append({it->header, it->last});
  return out;
}

void FoldMap::rebuildHidden() {
  m_hidden.clear();
  qsizetype before = 0;
  for (const Fold &f : m_folds) {
    const qsizetype first = f.header + 1;
    if (!m_hidden.empty() && first <= m_hidden.back().last + 1) {
      Hidden &h = m_hidden.back();
      if (f.last > h.last)
        h.last = f.last;
    } else {
      if (!m_hidden.empty())
        before = m_hidden.back().before + m_hidden.back().count();
      m_hidden.push_back({first, f.last, before});
    }
  }
}

FoldMap::Fold FoldMap::makeFold(qsizetype header, qsizetype lastLine, qsizetype headerEnd, qsizetype lastEnd) {
  const Rope &rope = m_document->rope();
  Fold f;
  f.header = header;
  f.last = lastLine;
  f.start = m_document->anchors().create(headerEnd >= 0 ? headerEnd : rope.lineEnd(header), Gravity::Left);
  f.end = m_document->anchors().create(lastEnd >= 0 ? lastEnd : rope.lineEnd(lastLine), Gravity::Left);
  return f;
}

LineRange FoldMap::fold(qsizetype header, qsizetype lastLine) {
  LineRange changed;
  if (header < 0 || lastLine <= header || lastLine >= bufferLineCount())
    return changed;
  auto it = std::lower_bound(
    m_folds.begin(), m_folds.end(), header, [](const Fold &f, qsizetype h) { return f.header < h; }
  );
  if (it != m_folds.end() && it->header == header) {
    if (it->last == lastLine)
      return changed;
    changed.unite(header + 1, qMax(it->last, lastLine));
    dropAnchors(*it);
    it = m_folds.erase(it);
  } else {
    changed.unite(header + 1, lastLine);
  }
  m_folds.insert(it, makeFold(header, lastLine));
  rebuildHidden();
  return changed;
}

LineRange FoldMap::unfold(qsizetype header) {
  LineRange changed;
  const auto cit = findHeader(header);
  if (cit == m_folds.end() || cit->header != header)
    return changed;
  changed.unite(cit->header + 1, cit->last);
  const auto it = m_folds.begin() + (cit - m_folds.cbegin());
  dropAnchors(*it);
  m_folds.erase(it);
  rebuildHidden();
  return changed;
}

LineRange FoldMap::unfoldContaining(qsizetype line) {
  LineRange changed;
  if (!isHidden(line))
    return changed;
  std::vector<Fold> kept;
  kept.reserve(m_folds.size());
  for (const Fold &f : m_folds) {
    if (f.header < line && line <= f.last) {
      changed.unite(f.header + 1, f.last);
      dropAnchors(f);
    } else {
      kept.push_back(f);
    }
  }
  m_folds = std::move(kept);
  rebuildHidden();
  return changed;
}

LineRange FoldMap::unfoldAll() {
  LineRange changed;
  if (m_folds.empty())
    return changed;
  changed.unite(m_folds.front().header + 1, bufferLineCount() - 1);
  for (const Fold &f : m_folds)
    dropAnchors(f);
  m_folds.clear();
  m_hidden.clear();
  return changed;
}

LineRange FoldMap::setFolds(const QList<FoldRange> &ranges) {
  LineRange changed;
  if (!m_folds.empty())
    changed.unite(m_folds.front().header + 1, bufferLineCount() - 1);
  for (const Fold &f : m_folds)
    dropAnchors(f);
  m_folds.clear();

  QList<FoldRange> sorted = ranges;
  std::stable_sort(sorted.begin(), sorted.end(), [](const FoldRange &a, const FoldRange &b) {
    return a.startLine < b.startLine;
  });
  const qsizetype lines = bufferLineCount();
  m_folds.reserve(size_t(sorted.size()));
  QList<FoldRange> valid;
  valid.reserve(sorted.size());
  qsizetype lastHeader = -1;
  for (const FoldRange &r : std::as_const(sorted)) {
    if (r.startLine < 0 || r.endLine <= r.startLine || r.endLine >= lines || r.startLine == lastHeader)
      continue;
    lastHeader = r.startLine;
    valid.append(r);
    changed.unite(r.startLine + 1, r.endLine);
  }
  // Many folds: find the line ends in one sweep of the text instead of one lookup each.
  QList<qsizetype> wanted, ends;
  if (valid.size() > 64) {
    wanted.reserve(valid.size() * 2);
    for (const FoldRange &r : std::as_const(valid))
      wanted << r.startLine << r.endLine;
    std::sort(wanted.begin(), wanted.end());
    wanted.erase(std::unique(wanted.begin(), wanted.end()), wanted.end());
    ends = lineEndOffsets(m_document->rope(), wanted);
  }
  auto endOf = [&](qsizetype line) {
    return wanted.isEmpty() ? qsizetype(-1) : ends[std::lower_bound(wanted.begin(), wanted.end(), line) - wanted.begin()];
  };
  for (const FoldRange &r : std::as_const(valid))
    m_folds.push_back(makeFold(r.startLine, r.endLine, endOf(r.startLine), endOf(r.endLine)));
  rebuildHidden();
  return changed;
}

LineRange FoldMap::applyChange(const TextChange &change) {
  LineRange changed;
  const qsizetype first = change.startPos.line;
  const qsizetype oldLines = change.oldEndPos.line - first + 1;
  const qsizetype newLines = change.newEndPos.line - first + 1;
  // Without a line break added or removed no line keeps a different number, and anchors don't leave
  // their lines.
  if (m_folds.empty() || (oldLines == 1 && newLines == 1))
    return changed;

  const qsizetype delta = newLines - oldLines;
  const qsizetype oldEditLast = first + oldLines - 1;
  const qsizetype newEditLast = first + newLines - 1;
  const Rope &rope = m_document->rope();
  const AnchorSet &anchors = m_document->anchors();
  // A line number from before the edit, in the text after it.
  auto mapOld = [&](qsizetype line) { return line > oldEditLast ? line + delta : qMin(line, newEditLast); };

  std::vector<Fold> out;
  out.reserve(m_folds.size());
  for (Fold f : m_folds) {
    if (f.last < first) {
      out.push_back(f);
      continue;
    }
    if (f.header > oldEditLast) {
      f.header += delta;
      f.last += delta;
      out.push_back(f);
      continue;
    }
    const qsizetype oldHeader = f.header, oldLast = f.last;
    f.header = rope.positionAt(anchors.offset(f.start)).line;
    f.last = rope.positionAt(anchors.offset(f.end)).line;
    changed.unite(qMin(oldHeader, f.header) + 1, qMax(mapOld(oldLast), f.last));
    const bool duplicate = !out.empty() && out.back().header == f.header;
    if (f.last <= f.header || duplicate) {
      dropAnchors(f);
      continue;
    }
    out.push_back(f);
  }
  m_folds = std::move(out);
  rebuildHidden();
  return changed;
}

} // namespace qce
