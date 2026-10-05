#ifndef QCE_COMMANDS_H
#define QCE_COMMANDS_H

#include "core/cursorlayout.h"
#include "core/displaymap.h"
#include "core/selectionset.h"
#include "core/textdocument.h"

#include <QtCore/QList>

#include <optional>
#include <utility>

namespace qce {

// Editor behaviour that commands depend on; the host sets it from its properties.
struct EditorSettings {
  bool insertSpaces = true; // Tab inserts spaces rather than a tab character
  int indentWidth = 4;
  int tabWidth = 4;
  bool readOnly = false;
  // Movement steps over folded lines instead of landing in them (needs `map`). Off, the cursor can
  // enter a fold, and the editor then unfolds it.
  bool skipFolds = true;
  // Select-all-occurrences stops here: every selection costs two anchors, and a million of them
  // is a hang rather than a feature.
  int maxSelections = 100000;
  // Bracket and quote pairing (typeText, Backspace, Enter). A pair whose two characters are equal
  // is a quote: it only closes at a word boundary and is not expanded by Enter.
  bool autoClose = true;
  QList<std::pair<char16_t, char16_t>> autoClosePairs = {
    {u'(', u')'}, {u'[', u']'}, {u'{', u'}'}, {u'"', u'"'}, {u'\'', u'\''}, {u'`', u'`'},
  };
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

// Typing from the keyboard or an input method. Like insertText, but with `autoClose` a single
// opener inserts its closer (wrapping a selection instead), and a closer typed in front of the same
// closer steps over it. Anything else is insertText.
bool typeText(EditContext &ctx, QStringView text);

// Paste. With several selections the text is distributed one piece per selection when the pieces
// (given by the clipboard, else the lines of `text` without a final line break) are as many as the
// selections; otherwise every selection receives all of `text`.
bool paste(EditContext &ctx, const QString &text, const QStringList &pieces = {});

// Enter: replaces the selections with a line break and the indentation of the line it was typed on
// (as far as the cursor reaches into it). With `autoClose`, Enter between an empty bracket pair
// opens a line for the cursor, one indent unit deeper, and leaves the closer on the line below.
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

// Adds a cursor one display row above the topmost selection (or below the bottom-most), at its goal
// x, and makes it primary. False at the first/last row or without a map and layout.
bool addCursorVertical(EditContext &ctx, bool up);
// Ctrl+D: with an empty primary selection, selects the word under every empty cursor; otherwise
// adds the next occurrence of the primary selection's text (after it, wrapping around, skipping
// text that is already selected) as a new primary selection. The match is a whole word when the
// selected text is one. False when there is nothing to select or no further occurrence.
bool addNextOccurrence(EditContext &ctx);
// Selects every occurrence of the primary selection's text (of the word under an empty cursor),
// up to settings.maxSelections; `*capped` says the limit cut it short. The primary stays the first
// occurrence at or after the old one.
bool selectAllOccurrences(EditContext &ctx, bool *capped = nullptr);
// Column selection: one selection per display row from `anchorRow` to `headRow`, each from the
// offset nearest `anchorX` to the one nearest `headX` (content x, per CursorLayout). Rows too short
// to reach the box get a selection clamped to their end, so typing lands on every row. The primary
// is the head row's. Clamped to settings.maxSelections rows around the head. No map or layout:
// does nothing and returns false.
bool boxSelect(EditContext &ctx, qsizetype anchorRow, qreal anchorX, qsizetype headRow, qreal headX);
// Keeps only the primary selection; false when there is just one.
bool collapseSelections(EditContext &ctx);

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
