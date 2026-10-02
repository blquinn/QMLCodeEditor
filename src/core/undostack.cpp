#include "core/undostack.h"

#include <QtCore/QElapsedTimer>

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
      top.edits.append(edit);
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

void UndoStack::endGroup(const SelectionList &after) {
  Q_ASSERT(m_depth > 0);
  if (--m_depth > 0)
    return;
  if (m_group.edits.isEmpty())
    return;
  m_group.selectionsAfter = after;
  m_group.kind = EditKind::Other;
  m_group.timeMs = now();
  m_group.sealed = true;
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
