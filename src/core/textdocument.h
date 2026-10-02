#ifndef QCE_TEXTDOCUMENT_H
#define QCE_TEXTDOCUMENT_H

#include "core/anchorset.h"
#include "core/fileformat.h"
#include "core/textchange.h"
#include "core/textsnapshot.h"
#include "core/undostack.h"

#include <QtCore/QObject>

#include <optional>

namespace qce {

class FileLoadJob;

// How an edit is recorded in the undo history.
struct EditOptions {
  EditKind kind = EditKind::Other; // Typing/DeleteBackward/DeleteForward edits can merge into one step
  SelectionList selectionsBefore;  // restored by undo; ignored inside an edit group
  SelectionList selectionsAfter;   // restored by redo; ignored inside an edit group
};

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
  bool insert(qsizetype offset, QStringView text, const EditOptions &options = {});
  bool remove(qsizetype start, qsizetype end, const EditOptions &options = {});
  bool replace(qsizetype start, qsizetype end, QStringView text, const EditOptions &options = {});
  bool replace(qsizetype start, qsizetype end, const Rope &text, const EditOptions &options = {});

  // Undo (CORE-07). Edits made between beginEditGroup and endEditGroup form one undo step; groups
  // nest and only the outermost records selections. undo()/redo() apply ordinary edits (so anchors
  // and change listeners stay in sync) and return the selections to restore, or nullopt when there
  // is nothing to do or a group is open. Resetting the text clears the history.
  void beginEditGroup(const SelectionList &before = {});
  void endEditGroup(const SelectionList &after = {});
  bool canUndo() const { return !m_loading && !m_undo.inGroup() && m_undo.canUndo(); }
  bool canRedo() const { return !m_loading && !m_undo.inGroup() && m_undo.canRedo(); }
  std::optional<SelectionList> undo();
  std::optional<SelectionList> redo();
  void breakUndoCoalescing() { m_undo.breakCoalescing(); }
  UndoStack &undoStack() { return m_undo; }

  // Loading (CORE-08). The file is decoded on a worker thread and appended as it arrives: each
  // slice is announced as an ordinary changed() insertion at the end, so listeners see the document
  // grow. While loading, edits are refused. load() clears the current text first (textReset()).
  void load(const QString &path);
  void cancelLoad();
  bool isLoading() const { return m_loading; }
  // How the loaded file was encoded; used when saving. Documents not loaded from a file default to
  // UTF-8, no BOM, LF.
  const FileFormat &format() const { return m_format; }
  void setFormat(const FileFormat &format) { m_format = format; }

  // Writes the current text to `path` in format() (CORE-09), atomically. Blocks; for large files
  // call saveFile() from a worker with snapshot().rope() instead.
  bool save(const QString &path, QString *error = nullptr) const;

  // Replaces the whole text without a change event for each edit; emits textReset().
  void setText(QStringView text);
  void reset(const Rope &rope);

signals:
  void loadProgress(qint64 bytesDone, qint64 bytesTotal);
  void loadFinished(const qce::FileFormat &format);
  void loadFailed(const QString &error);
  void changed(const qce::TextChange &change);
  // The text was replaced wholesale (load, setText); anything position-based must be rebuilt.
  void textReset();

private:
  void appendLoaded(const Rope &full, bool final);
  bool apply(qsizetype start, qsizetype end, const Rope &text, const EditOptions *record);

  Rope m_rope;
  AnchorSet m_anchors;
  UndoStack m_undo;
  FileFormat m_format;
  FileLoadJob *m_job = nullptr;
  bool m_loading = false;
  quint64 m_version = 0;
};

} // namespace qce

#endif // QCE_TEXTDOCUMENT_H
