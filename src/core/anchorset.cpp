#include "core/anchorset.h"

#include <algorithm>

namespace qce {

namespace {
constexpr size_t kMaxBlock = 512;
constexpr size_t kTargetBlock = 256;
} // namespace

AnchorSet::AnchorSet() = default;
AnchorSet::~AnchorSet() = default;

size_t AnchorSet::firstBlockEndingAtOrAfter(qsizetype offset) const {
  auto it = std::partition_point(m_blocks.begin(), m_blocks.end(), [&](const std::unique_ptr<Block> &b) {
    return b->real(b->items.back()) < offset;
  });
  return size_t(it - m_blocks.begin());
}

void AnchorSet::reindex(Block *block, size_t from) {
  for (size_t i = from; i < block->items.size(); ++i) {
    Slot &s = m_slots[block->items[i].id - 1];
    s.block = block;
    s.index = int(i);
  }
}

// Carves a block that grew past kMaxBlock into blocks of about kTargetBlock entries.
void AnchorSet::splitBlock(size_t blockIndex) {
  Block *block = m_blocks[blockIndex].get();
  if (block->items.size() <= kMaxBlock)
    return;
  std::vector<std::unique_ptr<Block>> pieces;
  for (size_t at = kTargetBlock; at < block->items.size(); at += kTargetBlock) {
    auto piece = std::make_unique<Block>();
    piece->shift = block->shift;
    const size_t end = std::min(at + kTargetBlock, block->items.size());
    piece->items.assign(block->items.begin() + qptrdiff(at), block->items.begin() + qptrdiff(end));
    reindex(piece.get(), 0);
    pieces.push_back(std::move(piece));
  }
  block->items.resize(kTargetBlock);
  m_blocks.insert(
    m_blocks.begin() + qptrdiff(blockIndex) + 1, std::make_move_iterator(pieces.begin()),
    std::make_move_iterator(pieces.end())
  );
}

AnchorId AnchorSet::create(qsizetype offset, Gravity gravity) {
  offset = qMax<qsizetype>(offset, 0);
  AnchorId id;
  if (m_freeSlot >= 0) {
    id = AnchorId(m_freeSlot + 1);
    m_freeSlot = m_slots[size_t(m_freeSlot)].nextFree;
  } else {
    m_slots.emplace_back();
    id = AnchorId(m_slots.size());
  }

  size_t b;
  if (m_blocks.empty()) {
    m_blocks.push_back(std::make_unique<Block>());
    b = 0;
  } else {
    b = firstBlockEndingAtOrAfter(offset);
    if (b == m_blocks.size())
      b = m_blocks.size() - 1;
  }
  Block *block = m_blocks[b].get();
  auto pos = std::partition_point(block->items.begin(), block->items.end(), [&](const Entry &e) {
    return block->real(e) <= offset;
  });
  const size_t index = size_t(pos - block->items.begin());
  block->items.insert(pos, Entry{offset - block->shift, id, gravity});
  reindex(block, index);
  ++m_count;
  splitBlock(b);
  return id;
}

void AnchorSet::remove(AnchorId id) {
  if (!contains(id))
    return;
  Slot &slot = m_slots[id - 1];
  Block *block = slot.block;
  const size_t index = size_t(slot.index);
  block->items.erase(block->items.begin() + qptrdiff(index));
  reindex(block, index);
  slot = Slot{};
  slot.nextFree = m_freeSlot;
  m_freeSlot = int(id - 1);
  --m_count;
  if (block->items.empty()) {
    auto it = std::find_if(m_blocks.begin(), m_blocks.end(), [&](const auto &p) { return p.get() == block; });
    m_blocks.erase(it);
  }
}

bool AnchorSet::contains(AnchorId id) const {
  return id != InvalidAnchor && id <= m_slots.size() && m_slots[id - 1].block != nullptr;
}

qsizetype AnchorSet::offset(AnchorId id) const {
  Q_ASSERT(contains(id));
  const Slot &s = m_slots[id - 1];
  return s.block->real(s.block->items[size_t(s.index)]);
}

Gravity AnchorSet::gravity(AnchorId id) const {
  Q_ASSERT(contains(id));
  const Slot &s = m_slots[id - 1];
  return s.block->items[size_t(s.index)].gravity;
}

void AnchorSet::clear() {
  m_blocks.clear();
  m_slots.clear();
  m_freeSlot = -1;
  m_count = 0;
}

void AnchorSet::applyEdit(qsizetype start, qsizetype oldEnd, qsizetype newEnd) {
  const qsizetype delta = newEnd - oldEnd;
  if (m_blocks.empty() || (delta == 0 && start == oldEnd))
    return;
  const size_t b = firstBlockEndingAtOrAfter(start);
  if (b == m_blocks.size())
    return; // every anchor is before the edit

  Block *first = m_blocks[b].get();
  const size_t lo = size_t(
    std::partition_point(
      first->items.begin(), first->items.end(), [&](const Entry &e) { return first->real(e) < start; }
    ) -
    first->items.begin()
  );

  // Pull out every anchor in [start, oldEnd] (real offsets). The run may span several blocks.
  std::vector<Entry> run;
  size_t endBlock = m_blocks.size(); // block holding the first anchor past oldEnd, if any
  for (size_t cur = b, from = lo; cur < m_blocks.size(); ++cur, from = 0) {
    Block *blk = m_blocks[cur].get();
    size_t hi = from;
    while (hi < blk->items.size() && blk->real(blk->items[hi]) <= oldEnd) {
      Entry e = blk->items[hi];
      e.off = blk->real(e);
      run.push_back(e);
      ++hi;
    }
    const bool stopped = hi < blk->items.size();
    blk->items.erase(blk->items.begin() + qptrdiff(from), blk->items.begin() + qptrdiff(hi));
    if (stopped) {
      endBlock = cur;
      break;
    }
  }

  // Collapse the run. Left gravity goes to `start` (except at the old end of a non-empty removal),
  // everything else to `newEnd`. A stable partition keeps the run sorted.
  const bool nonEmptyRemoval = oldEnd > start;
  auto mid = std::stable_partition(run.begin(), run.end(), [&](const Entry &e) {
    return e.gravity == Gravity::Left && !(nonEmptyRemoval && e.off == oldEnd);
  });
  for (auto it = run.begin(); it != run.end(); ++it)
    it->off = it < mid ? start : newEnd;

  // Blocks wholly consumed by the run disappear; everything after the run moves by delta.
  if (endBlock == b) {
    for (size_t i = lo; i < first->items.size(); ++i)
      first->items[i].off += delta;
  } else {
    m_blocks.erase(m_blocks.begin() + qptrdiff(b) + 1, m_blocks.begin() + qptrdiff(endBlock));
    // the block that stopped the run lost its head, so its entries moved down
    if (b + 1 < m_blocks.size())
      reindex(m_blocks[b + 1].get(), 0);
  }
  for (size_t i = b + 1; i < m_blocks.size(); ++i)
    m_blocks[i]->shift += delta;

  for (Entry &e : run)
    e.off -= first->shift;
  first->items.insert(first->items.begin() + qptrdiff(lo), run.begin(), run.end());
  reindex(first, lo);
  splitBlock(b);
}

bool AnchorSet::validate() const {
  qsizetype count = 0;
  qsizetype previous = 0;
  for (const auto &blk : m_blocks) {
    if (blk->items.empty())
      return false;
    for (size_t i = 0; i < blk->items.size(); ++i) {
      const Entry &e = blk->items[i];
      const qsizetype real = blk->real(e);
      if (real < previous || real < 0)
        return false;
      previous = real;
      if (e.id == InvalidAnchor || e.id > m_slots.size())
        return false;
      const Slot &s = m_slots[e.id - 1];
      if (s.block != blk.get() || s.index != int(i))
        return false;
      ++count;
    }
  }
  return count == m_count;
}

} // namespace qce
