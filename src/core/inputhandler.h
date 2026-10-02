#ifndef QCE_INPUTHANDLER_H
#define QCE_INPUTHANDLER_H

#include "core/commands.h"

#include <QtGui/QKeyEvent>

namespace qce {

// What an input handler needs from the item that core cannot do itself.
class InputHost {
public:
  virtual ~InputHost() = default;
  virtual void copy() = 0;
  virtual void cut() = 0;
  virtual void paste() = 0;
  // Scrolls the view by whole rows (positive scrolls down), for page up/down.
  virtual void scrollRows(qsizetype rows) = 0;
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
};

// Conventional editing keys, via QKeySequence::StandardKey so platform shortcuts are right.
class DefaultInputHandler : public InputHandler {
public:
  bool keyPress(QKeyEvent *event, EditContext &ctx, InputHost &host) override;
};

} // namespace qce

#endif // QCE_INPUTHANDLER_H
