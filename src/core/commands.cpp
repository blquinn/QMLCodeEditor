#include "core/commands.h"

#include "core/textboundaries.h"
#include "core/textsearch.h"

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
      result.append({start + r.insertedLength(), start + r.insertedLength()});
    shift += r.insertedLength() - (r.end - r.start);
  }
  const int newPrimary = primary >= 0 ? primary : int(result.size()) - 1;

  SelectionSet::Batch batch(ctx.selections);
  if (replacements.size() == 1) {
    const Replacement &r = replacements.first();
    if (r.rope)
      doc.replace(r.start, r.end, *r.rope, {kind, before, result});
    else
      doc.replace(r.start, r.end, r.text, {kind, before, result});
  } else {
    doc.beginEditGroup(before);
    for (qsizetype i = replacements.size() - 1; i >= 0; --i) {
      const Replacement &r = replacements[i];
      if (r.rope)
        doc.replace(r.start, r.end, *r.rope);
      else
        doc.replace(r.start, r.end, r.text);
    }
    doc.endEditGroup(result, kind);
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
  const QString eol = crlf ? QStringLiteral("\r\n") : QStringLiteral("\n");
  const Rope &rope = ctx.document.rope();
  QList<Replacement> list;
  for (int i = 0; i < ctx.selections.count(); ++i) {
    const Selection s = ctx.selections.at(i);
    const qsizetype line = rope.lineAt(s.start());
    const qsizetype lineStart = rope.lineStart(line);
    qsizetype end = lineStart;
    while (end < s.start() && (rope.at(end) == u' ' || rope.at(end) == u'\t'))
      ++end;
    list.append({s.start(), s.end(), eol + rope.toString(lineStart, end)});
  }
  return applyReplacements(ctx, list, EditKind::Other);
}

namespace {

// Cell column of `offset` on its line, with tab stops every `tabWidth` cells.
qsizetype visualColumn(const Rope &rope, qsizetype offset, int tabWidth) {
  const qsizetype start = rope.lineStart(rope.lineAt(offset));
  qsizetype cell = 0;
  for (qsizetype i = start; i < offset; ++i)
    cell += rope.at(i) == u'\t' ? tabWidth - cell % tabWidth : 1;
  return cell;
}

QString indentUnit(const EditorSettings &settings) {
  return settings.insertSpaces ? QString(settings.indentWidth, u' ') : QStringLiteral("\t");
}

// The lines a set of selections covers, ascending and without repeats. A selection that ends at the
// very start of a line does not include that line.
QList<qsizetype> touchedLines(EditContext &ctx) {
  const Rope &rope = ctx.document.rope();
  QList<qsizetype> lines;
  for (int i = 0; i < ctx.selections.count(); ++i) {
    const Selection s = ctx.selections.at(i);
    const qsizetype first = rope.lineAt(s.start());
    qsizetype last = rope.lineAt(s.end());
    if (!s.isEmpty() && last > first && s.end() == rope.lineStart(last))
      --last;
    for (qsizetype line = qMax(first, lines.isEmpty() ? first : lines.last() + 1); line <= last; ++line)
      lines.append(line);
  }
  return lines;
}

// Applies one edit per line, back to front, keeping the selections on the anchors that follow the
// edits (so a selection keeps covering the same text) and recording them for undo.
template <typename EditFn> bool editLines(EditContext &ctx, const QList<qsizetype> &lines, EditFn edit) {
  TextDocument &doc = ctx.document;
  if (doc.isLoading() || ctx.settings.readOnly)
    return false;
  const SelectionList before = ctx.selections.selections();
  const quint64 version = doc.version();
  SelectionSet::Batch batch(ctx.selections);
  doc.beginEditGroup(before);
  for (qsizetype i = lines.size() - 1; i >= 0; --i)
    edit(lines[i]);
  doc.endEditGroup(ctx.selections.selections());
  return doc.version() != version;
}

} // namespace

bool indent(EditContext &ctx) {
  const Rope &rope = ctx.document.rope();
  bool multiLine = false;
  for (int i = 0; i < ctx.selections.count() && !multiLine; ++i) {
    const Selection s = ctx.selections.at(i);
    multiLine = !s.isEmpty() && rope.lineAt(s.start()) != rope.lineAt(s.end()) &&
                !(rope.lineAt(s.end()) == rope.lineAt(s.start()) + 1 && s.end() == rope.lineStart(rope.lineAt(s.end())));
  }
  const EditorSettings &settings = ctx.settings;
  if (!multiLine) {
    QList<Replacement> list;
    for (int i = 0; i < ctx.selections.count(); ++i) {
      const Selection s = ctx.selections.at(i);
      QString text = QStringLiteral("\t");
      if (settings.insertSpaces) {
        const int width = qMax(1, settings.indentWidth);
        text = QString(width - visualColumn(rope, s.start(), settings.tabWidth) % width, u' ');
      }
      list.append({s.start(), s.end(), text});
    }
    return applyReplacements(ctx, list, EditKind::Other);
  }
  const QString unit = indentUnit(settings);
  return editLines(ctx, touchedLines(ctx), [&](qsizetype line) {
    // Blank lines stay blank: indentation there would only be trailing whitespace.
    if (ctx.document.rope().lineLength(line) > 0)
      ctx.document.insert(ctx.document.rope().lineStart(line), unit);
  });
}

bool outdent(EditContext &ctx) {
  const int width = qMax(1, ctx.settings.indentWidth);
  return editLines(ctx, touchedLines(ctx), [&](qsizetype line) {
    const Rope &rope = ctx.document.rope();
    const qsizetype start = rope.lineStart(line);
    qsizetype n = 0;
    if (rope.lineLength(line) > 0 && rope.at(start) == u'\t')
      n = 1;
    else
      while (n < width && n < rope.lineLength(line) && rope.at(start + n) == u' ')
        ++n;
    if (n > 0)
      ctx.document.remove(start, start + n);
  });
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
  // A head that landed in a folded line moves past the fold (forward) or back to its header's end.
  auto outOfFold = [&](qsizetype head, bool forward) {
    if (!ctx.map || !ctx.settings.skipFolds || !ctx.map->folds().hasFolds())
      return head;
    const FoldMap &folds = ctx.map->folds();
    const qsizetype line = rope.lineAt(head);
    if (!folds.isHidden(line))
      return head;
    const qsizetype next = folds.nextVisibleLine(line);
    if (forward && next < rope.lineCount())
      return rope.lineStart(next);
    return rope.lineEnd(folds.visibleHeaderOf(line));
  };
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
      head = outOfFold(head, movement == Movement::CharRight);
      break;
    case Movement::WordLeft:
      head = outOfFold(bounds.previousWordStart(s.head), false);
      break;
    case Movement::WordRight:
      head = outOfFold(bounds.nextWordStart(s.head), true);
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
    case Movement::RowStart: {
      const TextPosition pos = rope.positionAt(s.head);
      if (ctx.map) {
        const DisplayRow row = ctx.map->rowAt(ctx.map->rowForPosition(pos));
        if (!row.isFirst() && pos.column != row.startColumn) {
          head = rope.lineStart(pos.line) + row.startColumn; // the first press stays on the row
          break;
        }
      }
      const qsizetype indent = firstNonBlank(rope, pos.line);
      head = s.head == indent ? rope.lineStart(pos.line) : indent;
      break;
    }
    case Movement::RowEnd: {
      const TextPosition pos = rope.positionAt(s.head);
      head = rope.lineEnd(pos.line);
      if (ctx.map) {
        const DisplayRow row = ctx.map->rowAt(ctx.map->rowForPosition(pos));
        // The end of a row that continues is the last place a cursor can be on it; once there, End
        // goes on to the end of the line.
        const qsizetype rowEnd = rope.snapToCodePoint(rope.lineStart(pos.line) + row.lastCursorColumn());
        if (!row.isLast() && s.head != rowEnd)
          head = rowEnd;
      }
      break;
    }
    case Movement::DocStart:
      head = 0;
      break;
    case Movement::DocEnd:
      head = outOfFold(rope.length(), false);
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

bool addCursorVertical(EditContext &ctx, bool up) {
  if (!ctx.map || !ctx.layout)
    return false;
  const Rope &rope = ctx.document.rope();
  const int index = up ? 0 : ctx.selections.count() - 1;
  const Selection s = ctx.selections.at(index);
  const qsizetype row = ctx.map->rowForPosition(rope.positionAt(s.head));
  const qsizetype target = row + (up ? -1 : 1);
  if (target < 0 || target >= ctx.map->rowCount())
    return false;
  const qreal oldGoal = ctx.selections.goalX(index);
  const qreal goal = std::isnan(oldGoal) ? ctx.layout->xForOffset(s.head) : oldGoal;
  const qsizetype head = ctx.layout->offsetForX(ctx.map->rowAt(target), goal);
  ctx.document.breakUndoCoalescing();
  ctx.selections.setGoalX(index, goal); // the goal survives repeated presses
  ctx.selections.add({head, head});
  ctx.selections.setGoalX(ctx.selections.primaryIndex(), goal);
  return true;
}

namespace {

// The word at or just before `offset`, if there is one.
std::optional<Selection> wordAt(const Rope &rope, qsizetype offset) {
  const TextBoundaries bounds(rope);
  for (qsizetype probe : {offset, offset - 1}) {
    if (probe < 0 || probe >= rope.length())
      continue;
    const auto [start, end] = bounds.wordRangeAt(probe);
    if (end > start && search::isWordChar(rope.at(start)))
      return Selection{start, end};
  }
  return std::nullopt;
}

bool isWholeWord(const Rope &rope, Selection s) {
  if (s.isEmpty())
    return false;
  for (qsizetype i = s.start(); i < s.end(); ++i)
    if (!search::isWordChar(rope.at(i)))
      return false;
  return (s.start() == 0 || !search::isWordChar(rope.at(s.start() - 1))) &&
         (s.end() == rope.length() || !search::isWordChar(rope.at(s.end())));
}

constexpr qsizetype kMaxNeedle = 1 << 16;

} // namespace

bool addNextOccurrence(EditContext &ctx) {
  const Rope &rope = ctx.document.rope();
  SelectionSet &sel = ctx.selections;
  if (sel.primary().isEmpty()) {
    SelectionList list = sel.selections();
    bool any = false;
    for (Selection &s : list)
      if (s.isEmpty())
        if (const auto word = wordAt(rope, s.head)) {
          s = *word;
          any = true;
        }
    if (!any)
      return false;
    ctx.document.breakUndoCoalescing();
    sel.set(list, sel.primaryIndex());
    return true;
  }
  const Selection primary = sel.primary();
  if (primary.end() - primary.start() > kMaxNeedle)
    return false;
  const QString needle = rope.toString(primary.start(), primary.end());
  const search::Options options{true, isWholeWord(rope, primary)};
  qsizetype from = primary.end();
  for (int tries = 0; tries <= 2 * sel.count() + 2; ++tries) {
    const auto match = search::findNext(rope, needle, from, options, true);
    if (!match)
      return false;
    const int i = sel.lowerBound(match->start());
    if (i < sel.count() && sel.at(i).start() <= match->end()) {
      from = match->end(); // taken
      continue;
    }
    ctx.document.breakUndoCoalescing();
    sel.add(*match);
    return true;
  }
  return false;
}

bool selectAllOccurrences(EditContext &ctx, bool *capped) {
  if (capped)
    *capped = false;
  const Rope &rope = ctx.document.rope();
  SelectionSet &sel = ctx.selections;
  Selection primary = sel.primary();
  bool wholeWord = isWholeWord(rope, primary);
  if (primary.isEmpty()) {
    const auto word = wordAt(rope, primary.head);
    if (!word)
      return false;
    primary = *word;
    wholeWord = true;
  }
  if (primary.end() - primary.start() > kMaxNeedle)
    return false;
  const QString needle = rope.toString(primary.start(), primary.end());
  const SelectionList matches = search::findAll(rope, needle, qMax(1, ctx.settings.maxSelections), {true, wholeWord}, capped);
  if (matches.isEmpty())
    return false;
  int newPrimary = 0;
  while (newPrimary + 1 < matches.size() && matches[newPrimary].start() < primary.start())
    ++newPrimary;
  ctx.document.breakUndoCoalescing();
  sel.set(matches, newPrimary);
  return true;
}

bool boxSelect(EditContext &ctx, qsizetype anchorRow, qreal anchorX, qsizetype headRow, qreal headX) {
  if (!ctx.map || !ctx.layout)
    return false;
  const qsizetype last = ctx.map->rowCount() - 1;
  anchorRow = qBound<qsizetype>(0, anchorRow, last);
  headRow = qBound<qsizetype>(0, headRow, last);
  const qsizetype limit = qMax(1, ctx.settings.maxSelections);
  if (qAbs(headRow - anchorRow) >= limit)
    anchorRow = headRow + (anchorRow < headRow ? -(limit - 1) : limit - 1);
  const qsizetype first = qMin(anchorRow, headRow), end = qMax(anchorRow, headRow);
  SelectionList list;
  list.reserve(end - first + 1);
  for (qsizetype row = first; row <= end; ++row) {
    const DisplayRow displayRow = ctx.map->rowAt(row);
    list.append({ctx.layout->offsetForX(displayRow, anchorX), ctx.layout->offsetForX(displayRow, headX)});
  }
  ctx.document.breakUndoCoalescing();
  ctx.selections.set(list, int(headRow - first));
  return true;
}

bool collapseSelections(EditContext &ctx) {
  if (ctx.selections.count() < 2)
    return false;
  ctx.document.breakUndoCoalescing();
  ctx.selections.collapseToPrimary();
  return true;
}

} // namespace qce::commands
