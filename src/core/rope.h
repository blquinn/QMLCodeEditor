#ifndef QCE_ROPE_H
#define QCE_ROPE_H

#include <QtCore/QExplicitlySharedDataPointer>
#include <QtCore/QList>
#include <QtCore/QString>
#include <QtCore/QStringView>
#include <QtCore/QVarLengthArray>

#include "core/textposition.h"

namespace qce {

namespace detail {
struct Node;
}

// What every node knows about the text below it.
struct TextSummary {
  qsizetype length = 0;   // UTF-16 code units
  qsizetype newlines = 0; // '\n' units
};

// Persistent B-tree rope of UTF-16 text (ADR 0002, ADR 0007).
//
// A Rope is an immutable value: every edit returns a new Rope that shares unchanged nodes with the
// old one, so copying is O(1) and copies may be read from any thread. The tree is a plain sequence
// of UTF-16 units and makes no surrogate assumptions; leaf boundaries may split a pair. Use
// ChunkIterator to read text without ever seeing a split pair.
class Rope {
public:
  struct Stats {
    int height = 0; // 0 for an empty rope or a single leaf
    qsizetype leaves = 0;
    qsizetype branches = 0;
    qsizetype underfullLeaves = 0; // leaves below the minimum fill; fragmentation indicator
  };

  Rope();
  Rope(const Rope &other);
  Rope(Rope &&other) noexcept;
  Rope &operator=(const Rope &other);
  Rope &operator=(Rope &&other) noexcept;
  ~Rope();

  static Rope fromString(QStringView text);

  qsizetype length() const;
  bool isEmpty() const { return length() == 0; }
  qsizetype newlineCount() const;
  TextSummary summary() const;

  // Unit at `offset`, which must be in [0, length()).
  QChar at(qsizetype offset) const;

  // [start, end) clamped to the rope. O(log n).
  Rope slice(qsizetype start, qsizetype end) const;
  QString toString() const;
  QString toString(qsizetype start, qsizetype end) const;

  // Edits. Offsets are clamped to [0, length()]; they are not snapped to code points.
  Rope insert(qsizetype offset, QStringView text) const;
  Rope insert(qsizetype offset, const Rope &text) const;
  Rope remove(qsizetype start, qsizetype end) const;
  Rope replace(qsizetype start, qsizetype end, QStringView text) const;
  Rope concat(const Rope &other) const;

  // Lines and positions (ADR 0007). '\n' ends a line; a '\r' right before it belongs to the break.
  // All are O(log n) plus a scan inside one leaf.
  enum class Snap { Backward, Forward };
  qsizetype lineCount() const { return newlineCount() + 1; }
  // Offset of the first unit of `line` (clamped to the valid lines).
  qsizetype lineStart(qsizetype line) const;
  // Offset just past the last content unit of `line`, before its line break.
  qsizetype lineEnd(qsizetype line) const;
  qsizetype lineLength(qsizetype line) const { return lineEnd(line) - lineStart(line); }
  // Line containing `offset` (clamped). An offset inside a CRLF break belongs to the line it ends.
  qsizetype lineAt(qsizetype offset) const;
  // Offsets inside a CRLF break or a surrogate pair are snapped before conversion; the column is
  // never beyond the line's content.
  TextPosition positionAt(qsizetype offset) const;
  // Clamps the line to the document and the column to the line's content.
  qsizetype offsetAt(TextPosition pos) const;
  // Moves an offset that points between the halves of a surrogate pair to the pair's edge.
  qsizetype snapToCodePoint(qsizetype offset, Snap direction = Snap::Backward) const;

  // Identity of the root, for cheap "did anything change" checks.
  bool sharesRootWith(const Rope &other) const { return m_root == other.m_root; }

  // Checks the structural invariants (child heights, summaries, fan-out, leaf size limits).
  bool validate(QString *problem = nullptr) const;
  Stats stats() const;

  // Constants, exposed for tests and benchmarks.
  static constexpr qsizetype MaxLeafSize = 2048;
  static constexpr qsizetype MinLeafSize = 512;
  static constexpr int MaxChildren = 16;

private:
  friend class RopeBuilder;
  friend class ChunkIterator;
  using NodePtr = QExplicitlySharedDataPointer<detail::Node>;
  explicit Rope(NodePtr root);

  NodePtr m_root;
};

// Builds a Rope bottom-up in O(n) from a stream of text pieces.
class RopeBuilder {
public:
  RopeBuilder();
  ~RopeBuilder();
  RopeBuilder(const RopeBuilder &) = delete;
  RopeBuilder &operator=(const RopeBuilder &) = delete;

  void append(QStringView text);
  // Text appended so far, in units.
  qsizetype length() const { return m_length; }
  // Rope of everything appended so far; the builder can keep going afterwards.
  Rope snapshot() const;
  Rope finish();

private:
  struct Impl;
  Impl *d;
  qsizetype m_length = 0;
};

// Forward iteration over a rope's text in contiguous chunks that never end in the middle of a
// surrogate pair: when a pair straddles two leaves it is yielded as its own two-unit chunk.
// The start offset should be on a code point boundary. A chunk is valid until the next call.
class ChunkIterator {
public:
  explicit ChunkIterator(const Rope &rope, qsizetype from = 0);
  ~ChunkIterator();
  ChunkIterator(const ChunkIterator &) = delete;
  ChunkIterator &operator=(const ChunkIterator &) = delete;

  bool next(QStringView *chunk);

private:
  struct Frame {
    const detail::Node *node;
    int index; // next child to visit
  };
  bool nextLeaf(QStringView *leaf);
  bool peek();
  bool advance(QStringView *leaf);

  Rope m_rope; // keeps the nodes alive
  QVarLengthArray<Frame, 12> m_stack;
  QStringView m_current;
  QStringView m_peeked;
  bool m_hasPeeked = false;
  bool m_peekValid = false;
  QChar m_scratch[2];
};

} // namespace qce

#endif // QCE_ROPE_H
