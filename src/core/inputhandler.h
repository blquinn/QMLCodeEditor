#ifndef QCE_INPUTHANDLER_H
#define QCE_INPUTHANDLER_H

#include "core/commands.h"

#include <QtCore/QRegularExpression>
#include <QtGui/QKeyEvent>

namespace qce {

// Fold commands a key can ask the host for (the host owns the folds, the provider and the cursor).
enum class FoldCommand : quint8 { FoldAtCursor, UnfoldAtCursor, FoldAll, UnfoldAll };

// How the editor draws a cursor; the handler picks it (vim: block in normal mode, bar in insert).
enum class CursorShape : quint8 { Line, Block, Underline };

// What an input handler needs from the item that core cannot do itself.
class InputHost {
public:
  virtual ~InputHost() = default;
  virtual void copy() = 0;
  virtual void cut() = 0;
  virtual void paste() = 0;
  // Scrolls the view by whole rows (positive scrolls down), for page up/down.
  virtual void scrollRows(qsizetype rows) = 0;
  virtual void foldCommand(FoldCommand) {}
  // F8 / Shift+F8: go to the next or previous diagnostic.
  virtual void gotoDiagnostic(bool /*forward*/) {}

  // The display rows (inclusive) that are currently on screen; `valid` is false without a view.
  struct VisibleRows {
    qsizetype first = 0;
    qsizetype last = 0;
    bool valid = false;
  };
  virtual VisibleRows visibleRows() const { return {}; }
  // The system clipboard, or the selection clipboard where the platform has one (else the
  // clipboard). Used for the `+` and `*` registers.
  virtual QString clipboardText(bool /*selection*/ = false) { return {}; }
  virtual void setClipboardText(const QString & /*text*/, bool /*selection*/ = false) {}
  // Highlight every match of `pattern` in view (an invalid or empty pattern clears it).
  virtual void setSearchHighlight(const QRegularExpression & /*pattern*/) {}
};

// Turns events into commands (ADR 0005, ADR 0010). The default keymap and vim are two
// implementations; the editor holds one at a time and can swap it at runtime.
class InputHandler {
public:
  virtual ~InputHandler() = default;
  // Returns true when the event was used. The handler edits only through commands.
  virtual bool keyPress(QKeyEvent *event, EditContext &ctx, InputHost &host) = 0;
  // Forget any partial state (pending operator, count, ...), e.g. on focus loss.
  virtual void reset() {}

  // The editor switches handlers at runtime: the old one is deactivated (finish anything open, leave
  // the selections sane for the next handler), then the new one is activated.
  virtual void activate(EditContext &, InputHost &) {}
  virtual void deactivate(EditContext &, InputHost &) {}

  // How the cursors are drawn, and the offset of the character a cursor sits on (the editor draws
  // a block over it). `index` is the selection's index in the set. Line cursors sit at the head.
  virtual CursorShape cursorShape() const { return CursorShape::Line; }
  virtual qsizetype cursorOffset(int /*index*/, const Selection &selection, const EditContext &) const {
    return selection.head;
  }
  // False while keys must not become text (vim's normal mode): input methods are disabled then.
  virtual bool acceptsTextInput() const { return true; }
  // Text committed by an input method. The default types it.
  virtual bool commitText(const QString &text, EditContext &ctx, InputHost &host);
};

// Conventional editing keys, via QKeySequence::StandardKey so platform shortcuts are right.
class DefaultInputHandler : public InputHandler {
public:
  bool keyPress(QKeyEvent *event, EditContext &ctx, InputHost &host) override;
};

} // namespace qce

#endif // QCE_INPUTHANDLER_H
