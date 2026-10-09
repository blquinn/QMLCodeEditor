#ifndef QCE_LONGLINEINDEX_H
#define QCE_LONGLINEINDEX_H

#include "core/rope.h"
#include "core/wrapmeasure.h"

#include <QtCore/QtGlobal>

#include <memory>
#include <vector>

namespace qce {

namespace detail {
struct LongLineChunk {
  qsizetype start = 0; // units from the start of the line
  qreal x = 0;
  bool hasTab = false; // its width depends on where it starts
};
} // namespace detail

// Where the text of one very long line sits horizontally (PERF-01, ADR 0020). The editor shapes only the
// stretch of such a line that is on screen, so it cannot ask a layout where column N is or which column
// is at x. This index answers both from the rope: it cuts the line into chunks of about ChunkUnits UTF-16
// units and remembers the x each chunk starts at, measured with the host's WrapMeasure (a code point at a
// time, tabs to the next tab stop). A query finds the chunk by binary search and scans inside it.
//
// An index is immutable. afterEdit() returns the index for the edited rope, sharing nothing but costing
// O(chunks) (a few thousand for 5 MB) instead of a re-measure of the line. Widths are the sum of
// per-code-point advances, so they are exact for a monospace font and an approximation where shaping
// changes advances (kerning, ligatures, complex scripts).
class LongLineIndex {
public:
  static constexpr qsizetype ChunkUnits = 1024;

  // Index of the line that starts at rope offset `lineStart` and is `length` units long (no line break).
  static std::shared_ptr<const LongLineIndex>
  build(const Rope &rope, qsizetype lineStart, qsizetype length, const WrapMeasure &measure);

  // The index after replacing `removed` units at `column` of the line with `inserted` units; `rope` is
  // the document after the edit and `lineStart` the line's start in it. The edit must stay inside the
  // line (no line break removed or inserted); otherwise build a new index.
  std::shared_ptr<const LongLineIndex> afterEdit(
    const Rope &rope, qsizetype lineStart, qsizetype column, qsizetype removed, qsizetype inserted,
    const WrapMeasure &measure
  ) const;

  qsizetype length() const { return m_length; }
  // x just after the last unit of the line.
  qreal width() const { return m_width; }

  qsizetype chunkCount() const { return qsizetype(m_chunks.size()); }
  qsizetype chunkStart(qsizetype chunk) const { return m_chunks[size_t(chunk)].start; }
  qsizetype chunkEnd(qsizetype chunk) const {
    return chunk + 1 < chunkCount() ? m_chunks[size_t(chunk) + 1].start : m_length;
  }
  qreal chunkX(qsizetype chunk) const { return m_chunks[size_t(chunk)].x; }
  qreal chunkEndX(qsizetype chunk) const { return chunk + 1 < chunkCount() ? m_chunks[size_t(chunk) + 1].x : m_width; }
  // The chunk containing the unit at `column` (the last one for columns at or past the end).
  qsizetype chunkForColumn(qsizetype column) const;
  // The last chunk that starts at or before `x` (the first for x < 0).
  qsizetype chunkForX(qreal x) const;

  // x of the boundary before the unit at `column` (clamped to the line); a column inside a surrogate pair
  // gives the x before the pair. `rope` and `lineStart` must be the ones the index was built or updated for.
  qreal xForColumn(const Rope &rope, qsizetype lineStart, qsizetype column, const WrapMeasure &measure) const;
  // The column boundary nearest to `x`, clamped to the line.
  qsizetype columnForX(const Rope &rope, qsizetype lineStart, qreal x, const WrapMeasure &measure) const;

  // Checks that chunk starts and x positions agree with a fresh measure of the rope. For tests.
  bool validate(const Rope &rope, qsizetype lineStart, const WrapMeasure &measure, QString *problem = nullptr) const;

private:
  using Chunk = detail::LongLineChunk;

  LongLineIndex() = default;

  std::vector<Chunk> m_chunks;
  qsizetype m_length = 0;
  qreal m_width = 0;
};

} // namespace qce

#endif // QCE_LONGLINEINDEX_H
