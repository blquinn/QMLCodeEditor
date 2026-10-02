#ifndef QCE_UNDOSTACK_H
#define QCE_UNDOSTACK_H

#include "core/rope.h"
#include "core/selection.h"

#include <QtCore/QList>

#include <functional>

namespace qce {

// What kind of edit this is, for merging runs of typing or deleting into one undo step.
enum class EditKind : quint8 { Other, Typing, DeleteBackward, DeleteForward };

// One applied edit: [start, start + removed.length()) was replaced by `inserted`.
struct EditRecord {
  qsizetype start = 0;
  Rope removed;
  Rope inserted;
};

// One undo step: a list of edits applied in order, plus the selections to restore on either side.
struct Transaction {
  QList<EditRecord> edits;
  SelectionList selectionsBefore;
  SelectionList selectionsAfter;
  EditKind kind = EditKind::Other;
  qint64 timeMs = 0;
  bool sealed = false; // nothing more may be merged into this step
};

// Undo history. It knows nothing about the document: TextDocument feeds it edits and applies the
// inverse (undo) or the forward (redo) edits that it hands back.
class UndoStack {
public:
  using Clock = std::function<qint64()>; // milliseconds, monotonic

  explicit UndoStack(Clock clock = {});

  void setClock(Clock clock);
  void setCoalesceTimeoutMs(int ms) { m_timeoutMs = ms; }
  // Maximum number of undo steps kept; 0 means unlimited.
  void setLimit(int steps);

  // A standalone edit outside any group. Typing/DeleteBackward/DeleteForward edits that continue the
  // previous step (contiguous, within the timeout, no line break) are merged into it.
  void
  pushEdit(const EditRecord &edit, EditKind kind, const SelectionList &before, const SelectionList &after);

  // Groups collect several edits into a single step; they nest and only the outermost counts.
  void beginGroup(const SelectionList &before);
  void addToGroup(const EditRecord &edit);
  void endGroup(const SelectionList &after);
  bool inGroup() const { return m_depth > 0; }

  // The next edit starts a new step even if it would otherwise merge.
  void breakCoalescing();

  bool canUndo() const { return !m_undo.isEmpty(); }
  bool canRedo() const { return !m_redo.isEmpty(); }
  // Moves the newest step to the redo list and returns it; the caller applies its inverse.
  Transaction undo();
  // Moves the newest redo step back and returns it; the caller applies it forward.
  Transaction redo();
  void clear();

  int undoSteps() const { return int(m_undo.size()); }
  int redoSteps() const { return int(m_redo.size()); }

private:
  qint64 now() const;
  void push(Transaction t);

  Clock m_clock;
  int m_timeoutMs = 1000;
  int m_limit = 0;
  QList<Transaction> m_undo;
  QList<Transaction> m_redo;
  Transaction m_group;
  int m_depth = 0;
};

} // namespace qce

#endif // QCE_UNDOSTACK_H
