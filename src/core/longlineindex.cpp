#include "core/longlineindex.h"

#include <QtCore/QChar>

#include <algorithm>
#include <cmath>

namespace qce {

namespace {

constexpr qreal kEps = 1e-5;

// Advances of single code points, with the tab rule. ASCII is tabulated so the common case costs no
// virtual call.
struct Measurer {
  const WrapMeasure &measure;
  qreal tabDistance;
  qreal ascii[128];

  explicit Measurer(const WrapMeasure &m) : measure(m), tabDistance(qMax<qreal>(m.tabWidth() * m.cellAdvance(), 1e-3)) {
    for (int c = 0; c < 128; ++c)
      ascii[c] = m.advance(char32_t(c));
  }

  qreal nextTab(qreal x) const { return (std::floor((x + kEps) / tabDistance) + 1) * tabDistance; }

  qreal step(char32_t cp, qreal x, bool *tab) const {
    if (cp == u'\t') {
      *tab = true;
      return nextTab(x);
    }
    return x + (cp < 128 ? ascii[cp] : measure.advance(cp));
  }

  bool isTabMultiple(qreal shift) const {
    const qreal r = std::fmod(std::abs(shift), tabDistance);
    return r < kEps || tabDistance - r < kEps;
  }
};

// The code point at the start of `s` and how many units it takes.
inline char32_t codePointAt(QStringView s, qsizetype i, qsizetype *units) {
  const char16_t u = s[i].unicode();
  if (QChar::isHighSurrogate(u) && i + 1 < s.size() && QChar::isLowSurrogate(s[i + 1].unicode())) {
    *units = 2;
    return QChar::surrogateToUcs4(u, s[i + 1].unicode());
  }
  *units = 1;
  return u;
}

// Measures `count` units of the rope from `absStart`, starting at x. With `out`, also cuts chunks (the
// first starts at `relStart`, the line-relative position of absStart). Returns the end x.
qreal walk(
  const Rope &rope, qsizetype absStart, qsizetype count, qsizetype relStart, qreal x, const Measurer &mz,
  bool *anyTab, std::vector<detail::LongLineChunk> *out
) {
  ChunkIterator it(rope, absStart);
  QStringView slice;
  qsizetype done = 0, inChunk = 0;
  if (out)
    out->push_back({relStart, x, false});
  while (done < count && it.next(&slice)) {
    if (slice.size() > count - done)
      slice = slice.first(count - done);
    for (qsizetype i = 0; i < slice.size();) {
      qsizetype n;
      const char32_t cp = codePointAt(slice, i, &n);
      bool tab = false;
      x = mz.step(cp, x, &tab);
      if (tab) {
        if (anyTab)
          *anyTab = true;
        if (out)
          out->back().hasTab = true;
      }
      i += n;
      done += n;
      inChunk += n;
      if (out && inChunk >= LongLineIndex::ChunkUnits && count - done >= LongLineIndex::ChunkUnits / 2) {
        out->push_back({relStart + done, x, false});
        inChunk = 0;
      }
    }
  }
  return x;
}

} // namespace

std::shared_ptr<const LongLineIndex>
LongLineIndex::build(const Rope &rope, qsizetype lineStart, qsizetype length, const WrapMeasure &measure) {
  const Measurer mz(measure);
  std::shared_ptr<LongLineIndex> index(new LongLineIndex);
  index->m_length = length;
  index->m_chunks.reserve(size_t(length / ChunkUnits) + 1);
  index->m_width = walk(rope, lineStart, length, 0, 0, mz, nullptr, &index->m_chunks);
  return index;
}

qsizetype LongLineIndex::chunkForColumn(qsizetype column) const {
  const auto it = std::upper_bound(
    m_chunks.begin(), m_chunks.end(), column, [](qsizetype c, const Chunk &chunk) { return c < chunk.start; }
  );
  return qMax<qsizetype>(0, qsizetype(it - m_chunks.begin()) - 1);
}

qsizetype LongLineIndex::chunkForX(qreal x) const {
  const auto it = std::upper_bound(
    m_chunks.begin(), m_chunks.end(), x, [](qreal v, const Chunk &chunk) { return v < chunk.x; }
  );
  return qMax<qsizetype>(0, qsizetype(it - m_chunks.begin()) - 1);
}

qreal LongLineIndex::xForColumn(const Rope &rope, qsizetype lineStart, qsizetype column, const WrapMeasure &measure) const {
  if (column <= 0)
    return 0;
  if (column >= m_length)
    return m_width;
  const Measurer mz(measure);
  const qsizetype chunk = chunkForColumn(column);
  qreal x = m_chunks[size_t(chunk)].x;
  qsizetype pos = m_chunks[size_t(chunk)].start;
  ChunkIterator it(rope, lineStart + pos);
  QStringView slice;
  while (pos < column && it.next(&slice)) {
    for (qsizetype i = 0; i < slice.size() && pos < column;) {
      qsizetype n;
      const char32_t cp = codePointAt(slice, i, &n);
      if (pos + n > column) // inside a surrogate pair
        return x;
      bool tab = false;
      x = mz.step(cp, x, &tab);
      i += n;
      pos += n;
    }
  }
  return x;
}

qsizetype LongLineIndex::columnForX(const Rope &rope, qsizetype lineStart, qreal x, const WrapMeasure &measure) const {
  if (x <= 0)
    return 0;
  if (x >= m_width)
    return m_length;
  const Measurer mz(measure);
  const qsizetype chunk = chunkForX(x);
  qreal cur = m_chunks[size_t(chunk)].x;
  qsizetype pos = m_chunks[size_t(chunk)].start;
  const qsizetype end = chunkEnd(chunk);
  ChunkIterator it(rope, lineStart + pos);
  QStringView slice;
  while (pos < end && it.next(&slice)) {
    for (qsizetype i = 0; i < slice.size() && pos < end;) {
      qsizetype n;
      const char32_t cp = codePointAt(slice, i, &n);
      bool tab = false;
      const qreal next = mz.step(cp, cur, &tab);
      // Boundary `pos` is nearer than the one after this code point when x is left of the middle.
      if (x < (cur + next) * 0.5)
        return pos;
      cur = next;
      i += n;
      pos += n;
    }
  }
  return pos;
}

std::shared_ptr<const LongLineIndex> LongLineIndex::afterEdit(
  const Rope &rope, qsizetype lineStart, qsizetype column, qsizetype removed, qsizetype inserted,
  const WrapMeasure &measure
) const {
  const Measurer mz(measure);
  const qsizetype n = chunkCount();
  column = qBound<qsizetype>(0, column, m_length);
  removed = qBound<qsizetype>(0, removed, m_length - column);
  const qsizetype delta = inserted - removed;

  const qsizetype a = chunkForColumn(column);
  qsizetype b = qMax(a, chunkForColumn(column + removed));
  std::shared_ptr<LongLineIndex> index(new LongLineIndex);
  index->m_length = m_length + delta;
  index->m_chunks.reserve(m_chunks.size() + 2);

  // The region is the chunks the edit touches, plus the next one when what is left would be short, so
  // edits never leave a trail of small chunks. Chunks are cut at ChunkUnits unless that leaves under half
  // of one, so every chunk is between half and one and a half chunks long (except the last, or after a
  // deletion, until the next edit nearby).
  const qsizetype regionStart = m_chunks[size_t(a)].start;
  if (b + 1 < n && chunkEnd(b) + delta - regionStart < ChunkUnits / 2)
    ++b;
  const qsizetype regionLength = chunkEnd(b) + delta - regionStart;
  index->m_chunks.assign(m_chunks.begin(), m_chunks.begin() + a);
  qreal regionEndX = m_chunks[size_t(a)].x;
  if (regionLength > 0)
    regionEndX = walk(
      rope, lineStart + regionStart, regionLength, regionStart, regionEndX, mz, nullptr, &index->m_chunks
    );

  // Chunks after the region move by the edit's length change and by the change in width. A chunk with
  // a tab only changes width when the shift is not a whole number of tab stops.
  qreal shift = regionEndX - (b + 1 < n ? m_chunks[size_t(b) + 1].x : m_width);
  for (qsizetype j = b + 1; j < n; ++j) {
    Chunk c = m_chunks[size_t(j)];
    c.start += delta;
    if (std::abs(shift) > kEps) {
      c.x += shift;
      if (c.hasTab && !mz.isTabMultiple(shift)) {
        const qreal oldEndX = j + 1 < n ? m_chunks[size_t(j) + 1].x : m_width;
        const qreal newEnd =
          walk(rope, lineStart + c.start, chunkEnd(j) - m_chunks[size_t(j)].start, 0, c.x, mz, nullptr, nullptr);
        shift = newEnd - oldEndX;
      }
    }
    index->m_chunks.push_back(c);
  }
  index->m_width = (b + 1 < n ? m_width : regionEndX) + (b + 1 < n ? shift : 0);
  if (index->m_chunks.empty())
    index->m_chunks.push_back({0, 0, false});
  return index;
}

bool LongLineIndex::validate(const Rope &rope, qsizetype lineStart, const WrapMeasure &measure, QString *problem) const {
  auto fail = [&](const QString &why) {
    if (problem)
      *problem = why;
    return false;
  };
  const Measurer mz(measure);
  if (m_chunks.empty())
    return fail(QStringLiteral("no chunks"));
  if (m_chunks.front().start != 0 || qAbs(m_chunks.front().x) > kEps)
    return fail(QStringLiteral("first chunk does not start at 0"));
  for (qsizetype i = 0; i < chunkCount(); ++i) {
    const qsizetype start = chunkStart(i), end = chunkEnd(i);
    if (end < start || (i > 0 && end == start))
      return fail(QStringLiteral("chunk %1 is empty or backwards").arg(i));
    bool tab = false;
    const qreal endX = walk(rope, lineStart + start, end - start, 0, chunkX(i), mz, &tab, nullptr);
    const qreal expected = i + 1 < chunkCount() ? chunkX(i + 1) : m_width;
    if (std::abs(endX - expected) > 1e-3)
      return fail(QStringLiteral("chunk %1 ends at x=%2 but the next starts at %3").arg(i).arg(endX).arg(expected));
    if (tab != m_chunks[size_t(i)].hasTab)
      return fail(QStringLiteral("chunk %1 tab flag is wrong").arg(i));
  }
  return true;
}

} // namespace qce
