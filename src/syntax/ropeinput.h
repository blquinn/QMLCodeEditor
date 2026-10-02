#ifndef QCE_ROPEINPUT_H
#define QCE_ROPEINPUT_H

#include "core/rope.h"

#include <memory>

#include <tree_sitter/api.h>

namespace qce {

struct TreeDeleter {
  void operator()(TSTree *tree) const { ts_tree_delete(tree); }
};
using TreePtr = std::unique_ptr<TSTree, TreeDeleter>;

struct ParserDeleter {
  void operator()(TSParser *parser) const { ts_parser_delete(parser); }
};
using ParserPtr = std::unique_ptr<TSParser, ParserDeleter>;

// Feeds a rope to tree-sitter as UTF-16LE (SYNTAX-03): byte offsets are 2 x UTF-16 offsets, and a
// TSPoint column counts bytes. Reads are served from rope chunks without copying; the chunk handed
// out stays valid until the next read, as tree-sitter requires. Not thread-safe: one per parse.
class RopeInput {
public:
  explicit RopeInput(const Rope &rope) : m_rope(rope) {}

  TSInput input() { return TSInput{this, &RopeInput::read, TSInputEncodingUTF16LE, nullptr}; }

private:
  static const char *read(void *payload, uint32_t byteIndex, TSPoint position, uint32_t *bytesRead);
  const char *readAt(qsizetype unit, uint32_t *bytesRead);

  Rope m_rope;
  std::unique_ptr<ChunkIterator> m_chunks;
  qsizetype m_nextUnit = -1; // offset the iterator will yield next
};

// Offsets between the rope's UTF-16 units and tree-sitter's bytes.
inline uint32_t toByte(qsizetype unit) { return uint32_t(unit * 2); }
inline qsizetype toUnit(uint32_t byte) { return qsizetype(byte / 2); }

TSPoint toPoint(const TextPosition &pos);

} // namespace qce

#endif // QCE_ROPEINPUT_H
