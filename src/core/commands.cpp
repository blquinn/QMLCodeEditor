#include "core/commands.h"

#include "core/textboundaries.h"

#include <cmath>

namespace qce::commands {

bool applyReplacements(
  EditContext &ctx, const QList<Replacement> &replacements, EditKind kind, const QList<Selection> *after,
  int primary
) {
  TextDocument &doc = ctx.document;
  if (replacements.isEmpty() || doc.isLoading() || ctx.settings.readOnly)
    return false;
  const SelectionList before = ctx.selections.selections();

  // New selections: each replacement shifts everything after it by (inserted - removed).
  SelectionList result;
  result.reserve(replacements.size());
  qsizetype shift = 0;
  for (qsizetype i = 0; i < replacements.size(); ++i) {
    const Replacement &r = replacements[i];
    const qsizetype start = r.start + shift;
    if (after)
      result.append({start + (*after)[i].anchor, start + (*after)[i].head});
    else
      result.append({start + r.text.size(), start + r.text.size()});
    shift += r.text.size() - (r.end - r.start);
  }
  const int newPrimary = primary >= 0 ? primary : int(result.size()) - 1;

  SelectionSet::Batch batch(ctx.selections);
  if (replacements.size() == 1) {
    const Replacement &r = replacements.first();
    doc.replace(r.start, r.end, r.text, {kind, before, result});
  } else {
    doc.beginEditGroup(before);
    for (qsizetype i = replacements.size() - 1; i >= 0; --i)
      doc.replace(replacements[i].start, replacements[i].end, replacements[i].text);
    doc.endEditGroup(result);
  }
  ctx.selections.set(result, newPrimary);
  return true;
}

namespace {

// One replacement per selection. `range` returns what to remove for a selection and may shrink it
// (or return an empty range) when there is nothing to do.
template <typename RangeFn>
bool replaceEach(EditContext &ctx, const QString &text, EditKind kind, RangeFn range) {
  QList<Replacement> list;
  const int n = ctx.selections.count();
  list.reserve(n);
  qsizetype previousEnd = 0;
  bool changed = false;
  for (int i = 0; i < n; ++i) {
    const Selection s = ctx.selections.at(i);
    auto [start, end] = range(s);
    start = qMax(start, previousEnd); // neighbours' ranges never overlap
    end = qMax(start, end);
    previousEnd = end;
    changed |= start != end || !text.isEmpty();
    list.append({start, end, text}); // a no-op still keeps its cursor in the set
  }
  if (!changed)
    return false;
  return applyReplacements(ctx, list, kind);
}

} // namespace

bool insertText(EditContext &ctx, QStringView text, EditKind kind) {
  if (text.isEmpty() && ctx.selections.primary().isEmpty())
    return false;
  return replaceEach(ctx, text.toString(), kind, [](Selection s) { return qMakePair(s.start(), s.end()); });
}

bool deleteBackward(EditContext &ctx) {
  const TextBoundaries bounds(ctx.document.rope());
  bool anySelection = false;
  for (int i = 0; i < ctx.selections.count() && !anySelection; ++i)
    anySelection = !ctx.selections.at(i).isEmpty();
  return replaceEach(
    ctx, {}, anySelection ? EditKind::Other : EditKind::DeleteBackward,
    [&](Selection s) {
      return s.isEmpty() ? qMakePair(bounds.previousGrapheme(s.head), s.head) : qMakePair(s.start(), s.end());
    }
  );
}

bool deleteForward(EditContext &ctx) {
  const TextBoundaries bounds(ctx.document.rope());
  bool anySelection = false;
  for (int i = 0; i < ctx.selections.count() && !anySelection; ++i)
    anySelection = !ctx.selections.at(i).isEmpty();
  return replaceEach(
    ctx, {}, anySelection ? EditKind::Other : EditKind::DeleteForward,
    [&](Selection s) {
      return s.isEmpty() ? qMakePair(s.head, bounds.nextGrapheme(s.head)) : qMakePair(s.start(), s.end());
    }
  );
}

bool deleteSelection(EditContext &ctx) {
  return replaceEach(ctx, {}, EditKind::Other, [](Selection s) { return qMakePair(s.start(), s.end()); });
}

bool newline(EditContext &ctx) {
  // CRLF documents keep their convention for new lines.
  const bool crlf = ctx.document.format().dominantLineEnding == LineEnding::Crlf;
  return insertText(ctx, crlf ? u"\r\n" : u"\n", EditKind::Other);
}

bool deleteWordBackward(EditContext &ctx) {
  const TextBoundaries bounds(ctx.document.rope());
  return replaceEach(ctx, {}, EditKind::Other, [&](Selection s) {
    return s.isEmpty() ? qMakePair(bounds.previousWordStart(s.head), s.head) : qMakePair(s.start(), s.end());
  });
}

bool deleteWordForward(EditContext &ctx) {
  const TextBoundaries bounds(ctx.document.rope());
  return replaceEach(ctx, {}, EditKind::Other, [&](Selection s) {
    return s.isEmpty() ? qMakePair(s.head, bounds.nextWordStart(s.head)) : qMakePair(s.start(), s.end());
  });
}

bool selectAll(EditContext &ctx) {
  ctx.document.breakUndoCoalescing();
  ctx.selections.setSingle(0, ctx.document.length());
  return true;
}

bool undo(EditContext &ctx) {
  if (ctx.settings.readOnly)
    return false;
  SelectionSet::Batch batch(ctx.selections);
  const auto restored = ctx.document.undo();
  if (!restored)
    return false;
  if (!restored->isEmpty()) // edits made without selections keep the current ones (moved by anchors)
    ctx.selections.set(*restored, int(restored->size()) - 1);
  return true;
}

bool redo(EditContext &ctx) {
  if (ctx.settings.readOnly)
    return false;
  SelectionSet::Batch batch(ctx.selections);
  const auto restored = ctx.document.redo();
  if (!restored)
    return false;
  if (!restored->isEmpty()) // edits made without selections keep the current ones (moved by anchors)
    ctx.selections.set(*restored, int(restored->size()) - 1);
  return true;
}

namespace {

// First offset on the line that is not a space or tab (the line end when it is all blank).
qsizetype firstNonBlank(const Rope &rope, qsizetype line) {
  const qsizetype start = rope.lineStart(line), end = rope.lineEnd(line);
  qsizetype i = start;
  while (i < end && (rope.at(i) == u' ' || rope.at(i) == u'\t'))
    ++i;
  return i;
}

} // namespace

bool move(EditContext &ctx, Movement movement, bool extend) {
  const Rope &rope = ctx.document.rope();
  const TextBoundaries bounds(rope);
  const bool vertical = movement == Movement::RowUp || movement == Movement::RowDown ||
                        movement == Movement::PageUp || movement == Movement::PageDown;
  if (vertical && (!ctx.map || !ctx.layout))
    return false;

  const int n = ctx.selections.count();
  const SelectionList before = ctx.selections.selections();
  SelectionList after;
  QList<qreal> goals;
  after.reserve(n);
  goals.reserve(n);
  for (int i = 0; i < n; ++i) {
    const Selection s = before[i];
    qsizetype head = s.head;
    qreal goal = SelectionSet::NoGoal;
    switch (movement) {
    case Movement::CharLeft:
    case Movement::CharRight:
      if (!extend && !s.isEmpty()) {
        head = movement == Movement::CharLeft ? s.start() : s.end();
      } else {
        head = movement == Movement::CharLeft ? bounds.previousGrapheme(s.head) : bounds.nextGrapheme(s.head);
      }
      break;
    case Movement::WordLeft:
      head = bounds.previousWordStart(s.head);
      break;
    case Movement::WordRight:
      head = bounds.nextWordStart(s.head);
      break;
    case Movement::LineStart: {
      const qsizetype line = rope.lineAt(s.head);
      const qsizetype indent = firstNonBlank(rope, line);
      head = s.head == indent ? rope.lineStart(line) : indent;
      break;
    }
    case Movement::LineEnd:
      head = rope.lineEnd(rope.lineAt(s.head));
      break;
    case Movement::DocStart:
      head = 0;
      break;
    case Movement::DocEnd:
      head = rope.length();
      break;
    case Movement::RowUp:
    case Movement::RowDown:
    case Movement::PageUp:
    case Movement::PageDown: {
      const bool up = movement == Movement::RowUp || movement == Movement::PageUp;
      const qsizetype step = movement == Movement::RowUp || movement == Movement::RowDown ? 1 : ctx.layout->pageRows();
      const qsizetype row = ctx.map->rowForPosition(rope.positionAt(s.head));
      const qreal oldGoal = ctx.selections.goalX(i);
      goal = std::isnan(oldGoal) ? ctx.layout->xForOffset(s.head) : oldGoal;
      const qsizetype target = row + (up ? -step : step);
      if (target < 0)
        head = 0; // moving up from the first row goes to the start, like most editors
      else if (target >= ctx.map->rowCount())
        head = rope.length();
      else
        head = ctx.layout->offsetForX(ctx.map->rowAt(target), goal);
      break;
    }
    }
    after.append(extend ? Selection{s.anchor, head} : Selection{head, head});
    goals.append(goal);
  }
  ctx.document.breakUndoCoalescing();
  const int primary = ctx.selections.primaryIndex();
  if (after == before)
    return false;
  ctx.selections.set(after, primary);
  if (ctx.selections.count() == n)
    for (int i = 0; i < n; ++i)
      ctx.selections.setGoalX(i, goals[i]);
  return true;
}

} // namespace qce::commands
