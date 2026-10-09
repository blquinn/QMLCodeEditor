#include "core/undostack.h"

#include <QtCore/QElapsedTimer>

#include <algorithm>

namespace qce {

namespace {

qint64 monotonicMs() {
  static const QElapsedTimer origin = [] {
    QElapsedTimer t;
    t.start();
    return t;
  }();
  return origin.elapsed();
}

bool hasLineBreak(const EditRecord &e) {
  return e.inserted.newlineCount() > 0 || e.removed.newlineCount() > 0;
}

// Does `next` continue what `prev` was doing?
bool continues(const EditRecord &prev, const EditRecord &next, EditKind kind) {
  switch (kind) {
  case EditKind::Typing:
    return next.removed.isEmpty() && prev.removed.isEmpty() &&
           next.start == prev.start + prev.inserted.length();
  case EditKind::DeleteBackward:
    return next.inserted.isEmpty() && prev.inserted.isEmpty() &&
           next.start + next.removed.length() == prev.start;
  case EditKind::DeleteForward:
    return next.inserted.isEmpty() && prev.inserted.isEmpty() && next.start == prev.start;
  case EditKind::Other:
    break;
  }
  return false;
}

// Folds `next`, which continues `prev` (see continues()), into one record: the run is undone and redone in one
// replacement, and a thousand keystrokes hold one record instead of a thousand.
void mergeRecords(EditRecord &prev, const EditRecord &next, EditKind kind) {
  switch (kind) {
  case EditKind::Typing:
    prev.inserted = prev.inserted.concat(next.inserted);
    break;
  case EditKind::DeleteBackward:
    prev.removed = next.removed.concat(prev.removed);
    prev.start = next.start;
    break;
  case EditKind::DeleteForward:
    prev.removed = prev.removed.concat(next.removed);
    break;
  case EditKind::Other:
    break;
  }
}

// Adds a continuing edit (or group of edits) to the top step. A single edit that continues the step's last
// one is merged into it; several edits (one per cursor) are kept as they are.
void extend(Transaction &top, const QList<EditRecord> &edits, EditKind kind) {
  if (edits.size() == 1 && !top.edits.isEmpty() && continues(top.edits.last(), edits.first(), kind))
    mergeRecords(top.edits.last(), edits.first(), kind);
  else
    top.edits.append(edits);
}

} // namespace

UndoStack::UndoStack(Clock clock) : m_clock(std::move(clock)) {}

void UndoStack::setClock(Clock clock) { m_clock = std::move(clock); }

qint64 UndoStack::now() const { return m_clock ? m_clock() : monotonicMs(); }

void UndoStack::setLimit(int steps) {
  m_limit = steps;
  while (m_limit > 0 && m_undo.size() > m_limit)
    m_undo.removeFirst();
}

void UndoStack::push(Transaction t) {
  m_redo.clear();
  m_undo.append(std::move(t));
  if (m_limit > 0 && m_undo.size() > m_limit)
    m_undo.removeFirst();
}

void UndoStack::pushEdit(
  const EditRecord &edit, EditKind kind, const SelectionList &before, const SelectionList &after
) {
  Q_ASSERT(m_depth == 0);
  const qint64 t = now();
  const bool lineBreak = hasLineBreak(edit);
  if (kind != EditKind::Other && !lineBreak && !m_undo.isEmpty()) {
    Transaction &top = m_undo.last();
    if (
      !top.sealed && top.kind == kind && t - top.timeMs <= m_timeoutMs &&
      continues(top.edits.last(), edit, kind)
    ) {
      extend(top, {edit}, kind);
      top.selectionsAfter = after;
      top.timeMs = t;
      m_redo.clear();
      return;
    }
  }
  Transaction step;
  step.edits = {edit};
  step.selectionsBefore = before;
  step.selectionsAfter = after;
  step.kind = kind;
  step.timeMs = t;
  step.sealed = lineBreak; // a line break ends the step: what follows starts a fresh one
  push(std::move(step));
}

void UndoStack::beginGroup(const SelectionList &before) {
  if (m_depth++ == 0) {
    m_group = Transaction{};
    m_group.selectionsBefore = before;
  }
}

void UndoStack::addToGroup(const EditRecord &edit) {
  Q_ASSERT(m_depth > 0);
  m_group.edits.append(edit);
}

void UndoStack::endGroup(const SelectionList &after, EditKind kind) {
  Q_ASSERT(m_depth > 0);
  if (--m_depth > 0)
    return;
  if (m_group.edits.isEmpty())
    return;
  const qint64 t = now();
  const bool lineBreak = std::any_of(m_group.edits.begin(), m_group.edits.end(), hasLineBreak);
  const bool mergeable = kind != EditKind::Other && !lineBreak;
  // A run of multi-cursor typing or deleting is one step, like a run of single-cursor typing: the
  // group continues the top step when it starts from the selections that step ended with.
  if (mergeable && !m_undo.isEmpty()) {
    Transaction &top = m_undo.last();
    if (!top.sealed && top.kind == kind && t - top.timeMs <= m_timeoutMs && top.selectionsAfter == m_group.selectionsBefore) {
      extend(top, m_group.edits, kind);
      top.selectionsAfter = after;
      top.timeMs = t;
      m_redo.clear();
      m_group = Transaction{};
      return;
    }
  }
  m_group.selectionsAfter = after;
  m_group.kind = mergeable ? kind : EditKind::Other;
  m_group.timeMs = t;
  m_group.sealed = !mergeable;
  push(std::exchange(m_group, Transaction{}));
}

void UndoStack::breakCoalescing() {
  if (!m_undo.isEmpty())
    m_undo.last().sealed = true;
}

Transaction UndoStack::undo() {
  Q_ASSERT(canUndo());
  Transaction t = m_undo.takeLast();
  t.sealed = true;
  m_redo.append(t);
  return t;
}

Transaction UndoStack::redo() {
  Q_ASSERT(canRedo());
  Transaction t = m_redo.takeLast();
  m_undo.append(t);
  return t;
}

void UndoStack::clear() {
  m_undo.clear();
  m_redo.clear();
  m_group = Transaction{};
  m_depth = 0;
}

} // namespace qce
