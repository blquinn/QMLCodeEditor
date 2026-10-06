#include "core/bracketmatch.h"

#include "core/rope.h"

#include <QtCore/QString>

#include <algorithm>

namespace qce {
namespace {

constexpr qsizetype kBackwardBlock = 4096;

// The pair `ch` belongs to and whether it opens it; nullptr when it is no bracket.
const std::pair<char16_t, char16_t> *pairOf(char16_t ch, const BracketPairs &pairs, bool *opens) {
  for (const auto &pair : pairs) {
    if (pair.first == pair.second)
      continue;
    if (pair.first == ch) {
      *opens = true;
      return &pair;
    }
    if (pair.second == ch) {
      *opens = false;
      return &pair;
    }
  }
  return nullptr;
}

} // namespace

BracketPair findMatchingBracket(const Rope &rope, qsizetype offset, const BracketPairs &pairs, qsizetype maxScan) {
  if (offset < 0 || offset >= rope.length())
    return {};
  bool opens = false;
  const auto *pair = pairOf(rope.at(offset).unicode(), pairs, &opens);
  if (!pair)
    return {};
  const char16_t open = pair->first, close = pair->second;

  if (opens) {
    const qsizetype limit = qMin(rope.length(), offset + 1 + maxScan);
    qsizetype depth = 1, position = offset + 1;
    ChunkIterator it(rope, offset + 1);
    QStringView chunk;
    while (position < limit && it.next(&chunk)) {
      const qsizetype n = qMin<qsizetype>(chunk.size(), limit - position);
      for (qsizetype i = 0; i < n; ++i) {
        const char16_t ch = chunk[i].unicode();
        if (ch == open)
          ++depth;
        else if (ch == close && --depth == 0)
          return {offset, position + i};
      }
      position += n;
    }
    return {};
  }

  const qsizetype floor = qMax<qsizetype>(0, offset - maxScan);
  qsizetype depth = 1, end = offset;
  while (end > floor) {
    const qsizetype start = qMax(floor, end - kBackwardBlock);
    const QString block = rope.toString(start, end);
    for (qsizetype i = block.size() - 1; i >= 0; --i) {
      const char16_t ch = block[i].unicode();
      if (ch == close)
        ++depth;
      else if (ch == open && --depth == 0)
        return {start + i, offset};
    }
    end = start;
  }
  return {};
}

qsizetype bracketNearCursor(const Rope &rope, qsizetype head, const BracketPairs &pairs) {
  bool opens = false;
  if (head >= 0 && head < rope.length() && pairOf(rope.at(head).unicode(), pairs, &opens))
    return head;
  if (head > 0 && head <= rope.length() && pairOf(rope.at(head - 1).unicode(), pairs, &opens))
    return head - 1;
  return -1;
}

} // namespace qce
