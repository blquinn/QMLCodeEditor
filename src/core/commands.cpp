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
