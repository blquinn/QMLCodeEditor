#include "core/inputhandler.h"

#include <algorithm>

namespace qce {

namespace {

struct MoveKey {
  QKeySequence::StandardKey move;
  QKeySequence::StandardKey select;
  Movement movement;
};

constexpr MoveKey kMoves[] = {
  {QKeySequence::MoveToPreviousChar, QKeySequence::SelectPreviousChar, Movement::CharLeft},
  {QKeySequence::MoveToNextChar, QKeySequence::SelectNextChar, Movement::CharRight},
  {QKeySequence::MoveToPreviousWord, QKeySequence::SelectPreviousWord, Movement::WordLeft},
  {QKeySequence::MoveToNextWord, QKeySequence::SelectNextWord, Movement::WordRight},
  {QKeySequence::MoveToStartOfLine, QKeySequence::SelectStartOfLine, Movement::RowStart},
  {QKeySequence::MoveToEndOfLine, QKeySequence::SelectEndOfLine, Movement::RowEnd},
  {QKeySequence::MoveToStartOfDocument, QKeySequence::SelectStartOfDocument, Movement::DocStart},
  {QKeySequence::MoveToEndOfDocument, QKeySequence::SelectEndOfDocument, Movement::DocEnd},
  {QKeySequence::MoveToPreviousLine, QKeySequence::SelectPreviousLine, Movement::RowUp},
  {QKeySequence::MoveToNextLine, QKeySequence::SelectNextLine, Movement::RowDown},
  {QKeySequence::MoveToPreviousPage, QKeySequence::SelectPreviousPage, Movement::PageUp},
  {QKeySequence::MoveToNextPage, QKeySequence::SelectNextPage, Movement::PageDown},
};

// Row of the primary head, to scroll by however far a page move got.
qsizetype primaryRow(const EditContext &ctx) {
  return ctx.map ? ctx.map->rowForPosition(ctx.document.rope().positionAt(ctx.selections.primary().head)) : 0;
}

bool isPrintable(const QString &text, Qt::KeyboardModifiers modifiers) {
  if (text.isEmpty())
    return false;
  // Ctrl+letter arrives as a control character; AltGr arrives as Ctrl+Alt with real text.
  if ((modifiers & Qt::ControlModifier) && !(modifiers & Qt::AltModifier))
    return false;
  if ((modifiers & Qt::MetaModifier) && !(modifiers & Qt::ControlModifier))
    return false;
  return std::all_of(text.begin(), text.end(), [](QChar c) { return !c.isNonCharacter() && (c.unicode() >= 0x20 && c.unicode() != 0x7f); });
}

} // namespace

bool DefaultInputHandler::keyPress(QKeyEvent *event, EditContext &ctx, InputHost &host) {
  using namespace commands;

  if (event->matches(QKeySequence::Undo)) {
    undo(ctx);
    return true;
  }
  if (event->matches(QKeySequence::Redo)) {
    redo(ctx);
    return true;
  }
  if (event->matches(QKeySequence::SelectAll))
    return selectAll(ctx);
  if (event->matches(QKeySequence::Copy)) {
    host.copy();
    return true;
  }
  if (event->matches(QKeySequence::Cut)) {
    host.cut();
    return true;
  }
  if (event->matches(QKeySequence::Paste)) {
    host.paste();
    return true;
  }

  for (const MoveKey &key : kMoves) {
    const bool extend = event->matches(key.select);
    if (!extend && !event->matches(key.move))
      continue;
    const bool page = key.movement == Movement::PageUp || key.movement == Movement::PageDown;
    const qsizetype rowBefore = page ? primaryRow(ctx) : 0;
    move(ctx, key.movement, extend);
    if (page)
      host.scrollRows(primaryRow(ctx) - rowBefore);
    return true;
  }

  if (event->matches(QKeySequence::DeleteStartOfWord)) {
    deleteWordBackward(ctx);
    return true;
  }
  if (event->matches(QKeySequence::DeleteEndOfWord)) {
    deleteWordForward(ctx);
    return true;
  }
  switch (event->key()) {
  case Qt::Key_Backspace:
    deleteBackward(ctx);
    return true;
  case Qt::Key_Delete:
    deleteForward(ctx);
    return true;
  case Qt::Key_Tab:
    if (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
      return false;
    indent(ctx);
    return true;
  case Qt::Key_Backtab:
    outdent(ctx);
    return true;
  case Qt::Key_Return:
  case Qt::Key_Enter:
    newline(ctx);
    return true;
  default:
    break;
  }

  if (isPrintable(event->text(), event->modifiers())) {
    insertText(ctx, event->text());
    return true;
  }
  return false;
}

} // namespace qce
