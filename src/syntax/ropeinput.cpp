#include "syntax/ropeinput.h"

#include <QtCore/qsysinfo.h>

static_assert(Q_BYTE_ORDER == Q_LITTLE_ENDIAN, "the parse input hands out UTF-16 units as little-endian bytes");

namespace qce {

TSPoint toPoint(const TextPosition &pos) { return TSPoint{uint32_t(pos.line), uint32_t(pos.column * 2)}; }

const char *RopeInput::read(void *payload, uint32_t byteIndex, TSPoint, uint32_t *bytesRead) {
  return static_cast<RopeInput *>(payload)->readAt(toUnit(byteIndex), bytesRead);
}

const char *RopeInput::readAt(qsizetype unit, uint32_t *bytesRead) {
  *bytesRead = 0;
  if (unit >= m_rope.length())
    return "";
  if (!m_chunks || unit != m_nextUnit) {
    m_chunks = std::make_unique<ChunkIterator>(m_rope, unit);
    m_nextUnit = unit;
  }
  QStringView chunk;
  if (!m_chunks->next(&chunk))
    return "";
  m_nextUnit += chunk.size();
  *bytesRead = uint32_t(chunk.size() * 2);
  return reinterpret_cast<const char *>(chunk.utf16());
}

} // namespace qce
