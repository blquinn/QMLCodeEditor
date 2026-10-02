#ifndef QCE_ANCHORSET_H
#define QCE_ANCHORSET_H

#include <QtCore/QtGlobal>

#include <memory>
#include <vector>

namespace qce {

using AnchorId = quint32;
constexpr AnchorId InvalidAnchor = 0;

// Which way an anchor leans when text is inserted exactly at its offset: Left stays before the new
// text, Right moves after it (ADR 0006).
enum class Gravity : quint8 { Left, Right };

// Offsets that follow edits. Built for 100k+ live anchors: anchors live in sorted blocks of a few
// hundred entries, an edit rewrites only the anchors it touches and shifts every later block by
// adding to one number, so an edit costs O(log blocks + touched anchors + blocks).
//
// An edit replaces [start, oldEnd) with newEnd - start units. For an anchor at p:
//  - p < start: unchanged; p > oldEnd: moves by newEnd - oldEnd.
//  - p in [start, oldEnd]: collapses. An anchor at oldEnd of a non-empty removal goes to newEnd;
//    otherwise Left gravity goes to start and Right gravity goes to newEnd.
class AnchorSet {
public:
  AnchorSet();
  ~AnchorSet();
  AnchorSet(const AnchorSet &) = delete;
  AnchorSet &operator=(const AnchorSet &) = delete;

  AnchorId create(qsizetype offset, Gravity gravity = Gravity::Left);
  void remove(AnchorId id);
  bool contains(AnchorId id) const;
  qsizetype offset(AnchorId id) const;
  Gravity gravity(AnchorId id) const;
  qsizetype size() const { return m_count; }

  void applyEdit(qsizetype start, qsizetype oldEnd, qsizetype newEnd);
  void clear();

  // Checks ordering and bookkeeping; for tests.
  bool validate() const;

private:
  struct Entry {
    qsizetype off; // relative to the block's shift
    AnchorId id;
    Gravity gravity;
  };
  struct Block {
    std::vector<Entry> items;
    qsizetype shift = 0;
    qsizetype real(const Entry &e) const { return e.off + shift; }
  };
  struct Slot {
    Block *block = nullptr;
    int index = 0;
    int nextFree = -1;
  };

  size_t firstBlockEndingAtOrAfter(qsizetype offset) const;
  void reindex(Block *block, size_t from);
  void splitBlock(size_t blockIndex);

  std::vector<std::unique_ptr<Block>> m_blocks;
  std::vector<Slot> m_slots; // id - 1
  int m_freeSlot = -1;
  qsizetype m_count = 0;
};

} // namespace qce

#endif // QCE_ANCHORSET_H
