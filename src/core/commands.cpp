#include "core/commands.h"

#include "core/textboundaries.h"

namespace qce::commands {

bool applyReplacements(
  EditContext &ctx, const QList<Replacement> &replacements, EditKind kind, const QList<Selection> *after,
  int primary
) {
  TextDocument &doc = ctx.document;
  if (replacements.isEmpty() || doc.isLoading())
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

bool selectAll(EditContext &ctx) {
  ctx.document.breakUndoCoalescing();
  ctx.selections.setSingle(0, ctx.document.length());
  return true;
}

bool undo(EditContext &ctx) {
  SelectionSet::Batch batch(ctx.selections);
  const auto restored = ctx.document.undo();
  if (!restored)
    return false;
  if (!restored->isEmpty()) // edits made without selections keep the current ones (moved by anchors)
    ctx.selections.set(*restored, int(restored->size()) - 1);
  return true;
}

bool redo(EditContext &ctx) {
  SelectionSet::Batch batch(ctx.selections);
  const auto restored = ctx.document.redo();
  if (!restored)
    return false;
  if (!restored->isEmpty()) // edits made without selections keep the current ones (moved by anchors)
    ctx.selections.set(*restored, int(restored->size()) - 1);
  return true;
}

} // namespace qce::commands
