#ifndef QCE_TEXTSNAPSHOT_H
#define QCE_TEXTSNAPSHOT_H

#include "core/rope.h"

namespace qce {

// An immutable view of the document at one version. Copying is O(1) (a root pointer), and a copy
// may be read from any thread while the document keeps changing, because rope nodes are never
// mutated once shared.
class TextSnapshot {
public:
  TextSnapshot() = default;
  TextSnapshot(Rope rope, quint64 version) : m_rope(std::move(rope)), m_version(version) {}

  const Rope &rope() const { return m_rope; }
  quint64 version() const { return m_version; }

  qsizetype length() const { return m_rope.length(); }
  qsizetype lineCount() const { return m_rope.lineCount(); }
  QString toString() const { return m_rope.toString(); }
  QString toString(qsizetype start, qsizetype end) const { return m_rope.toString(start, end); }

private:
  Rope m_rope;
  quint64 m_version = 0;
};

} // namespace qce

#endif // QCE_TEXTSNAPSHOT_H
