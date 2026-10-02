#ifndef QCE_TEXTDOCUMENT_H
#define QCE_TEXTDOCUMENT_H

#include "core/anchorset.h"
#include "core/textchange.h"
#include "core/textsnapshot.h"

#include <QtCore/QObject>

namespace qce {

// Owns the current text. Every edit snaps its offsets to code point boundaries, bumps the version
// and emits changed() with a TextChange. Readers on other threads use snapshot().
class TextDocument : public QObject {
  Q_OBJECT
public:
  explicit TextDocument(QObject *parent = nullptr);
  ~TextDocument() override;

  TextSnapshot snapshot() const { return TextSnapshot(m_rope, m_version); }
  const Rope &rope() const { return m_rope; }
  quint64 version() const { return m_version; }
  qsizetype length() const { return m_rope.length(); }

  // Anchors follow every edit made through this document (CORE-06). A reset or setText counts as
  // replacing the whole text, so anchors collapse to the start or the end according to gravity.
  AnchorSet &anchors() { return m_anchors; }
  const AnchorSet &anchors() const { return m_anchors; }

  // Edits. Offsets are clamped, then a start inside a surrogate pair or a CRLF break moves back
  // to its start and an end inside one moves forward past it. Return false when the edit is refused.
  bool insert(qsizetype offset, QStringView text);
  bool remove(qsizetype start, qsizetype end);
  bool replace(qsizetype start, qsizetype end, QStringView text);
  bool replace(qsizetype start, qsizetype end, const Rope &text);

  // Replaces the whole text without a change event for each edit; emits textReset().
  void setText(QStringView text);
  void reset(const Rope &rope);

signals:
  void changed(const qce::TextChange &change);
  // The text was replaced wholesale (load, setText); anything position-based must be rebuilt.
  void textReset();

private:
  bool apply(qsizetype start, qsizetype end, const Rope &text);

  Rope m_rope;
  AnchorSet m_anchors;
  quint64 m_version = 0;
};

} // namespace qce

#endif // QCE_TEXTDOCUMENT_H
