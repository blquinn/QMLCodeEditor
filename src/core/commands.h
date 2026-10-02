#ifndef QCE_COMMANDS_H
#define QCE_COMMANDS_H

#include "core/cursorlayout.h"
#include "core/displaymap.h"
#include "core/selectionset.h"
#include "core/textdocument.h"

#include <optional>

namespace qce {

// Editor behaviour that commands depend on; the host sets it from its properties.
struct EditorSettings {
  bool insertSpaces = true; // Tab inserts spaces rather than a tab character
  int indentWidth = 4;
  int tabWidth = 4;
  bool readOnly = false;
};

// What a command acts on (ADR 0005): the document, the selections that always exist, and settings.
struct EditContext {
  TextDocument &document;
  SelectionSet &selections;
  EditorSettings settings;
  // Needed by vertical movement; those commands do nothing without them.
  const DisplayMap *map = nullptr;
  const CursorLayout *layout = nullptr;
};

enum class Movement : quint8 {
  CharLeft,
  CharRight,
  WordLeft,
  WordRight,
  LineStart, // first non-blank, then column 0 when already there
  LineEnd,
  RowStart, // like LineStart on the first row of a line; on a wrapped row the row's start, then LineStart
  RowEnd,   // the end of a wrapped row (just before the break), then LineEnd; LineEnd on the last row
  DocStart,
  DocEnd,
  RowUp, // by display row, keeping the goal x
  RowDown,
  PageUp,
  PageDown,
};

// Commands are the only code that edits the buffer on the user's behalf. Each applies to every
// selection in the set; a multi-selection edit is applied back to front inside one undo step and
// leaves a cursor after each replacement. They return false when nothing changed (read-only while
// loading, nothing to delete).
namespace commands {

// Replaces every selection with `text`. `kind` lets runs of typing merge into one undo step.
bool insertText(EditContext &ctx, QStringView text, EditKind kind = EditKind::Typing);
// Backspace and Delete: remove the selection, or the grapheme before/after an empty selection.
bool deleteBackward(EditContext &ctx);
bool deleteForward(EditContext &ctx);
// Removes the selected text (cut without the clipboard); empty selections are left alone.
bool deleteSelection(EditContext &ctx);

// Enter: replaces the selections with a line break and the indentation of the line it was typed on
// (as far as the cursor reaches into it).
bool newline(EditContext &ctx);
// Tab: indents the lines of any selection that spans several lines; otherwise replaces each
// selection with spaces up to the next indent stop (or a tab character, when !insertSpaces).
bool indent(EditContext &ctx);
// Shift+Tab: removes one level of indentation from every line a selection touches.
bool outdent(EditContext &ctx);

// Backspace and Delete for whole words.
bool deleteWordBackward(EditContext &ctx);
bool deleteWordForward(EditContext &ctx);

bool selectAll(EditContext &ctx);
// Moves every selection's head; with `extend` the anchors stay put. Without it a non-empty
// selection moved by character collapses to its edge in the direction of travel. Movement does not
// touch the text and ends the current typing run for undo.
bool move(EditContext &ctx, Movement movement, bool extend = false);

// Undo and redo restore the selections recorded with the step.
bool undo(EditContext &ctx);
bool redo(EditContext &ctx);

// One edit of a batch: [start, end) becomes `text`.
struct Replacement {
  Replacement(qsizetype start, qsizetype end, QString text = {})
      : start(start), end(end), text(std::move(text)) {}
  Replacement(qsizetype start, qsizetype end, Rope rope) : start(start), end(end), rope(std::move(rope)) {}

  qsizetype start = 0;
  qsizetype end = 0;
  QString text;
  // When set it is inserted instead of `text`: a big paste built off the GUI thread.
  std::optional<Rope> rope;
  qsizetype insertedLength() const { return rope ? rope->length() : text.size(); }
};
// The engine under the commands, for ones that live elsewhere (clipboard, indent). `replacements`
// must be sorted and non-overlapping. Afterwards each replacement's selection is a cursor at the end
// of its text (or `after`, when given, parallel to the replacements, offsets relative to the
// replacement's start in the new text).
bool applyReplacements(
  EditContext &ctx, const QList<Replacement> &replacements, EditKind kind, const QList<Selection> *after = nullptr,
  int primary = -1
);

} // namespace commands
} // namespace qce

#endif // QCE_COMMANDS_H
