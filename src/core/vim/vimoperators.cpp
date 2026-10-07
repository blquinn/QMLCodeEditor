// Operators, edits, registers, marks and jumps of the vim handler.
#include "core/vim/vimhandler.h"

#include "core/textboundaries.h"
#include "core/vim/vimkeys.h"
#include "core/vim/vimmotions.h"

#include <QtCore/QRegularExpression>

#include <algorithm>
#include <limits>

using namespace qce::vim;
using namespace Qt::StringLiterals;

namespace qce {

namespace {

QChar lowerOf(QChar c) { return c.toLower(); }

QString swapCase(const QString &text) {
  QString out;
  out.reserve(text.size());
  for (QChar c : text)
    out += c.isUpper() ? c.toLower() : c.isLower() ? c.toUpper() : c;
  return out;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Edits

VimInputHandler::EditResult VimInputHandler::edit(const QList<Edit> &edits) {
  EditResult result;
  if (edits.isEmpty())
    return result;
  QList<commands::Replacement> list;
  list.reserve(edits.size());
  qsizetype shift = 0;
  for (const Edit &e : edits) {
    list.append({e.start, e.end, e.text});
    const qsizetype start = e.start + shift;
    result.starts.append(start);
    result.ends.append(start + e.text.size());
    shift += e.text.size() - (e.end - e.start);
  }
  ensureGroup();
  result.ok = commands::applyReplacements(*m_ctx, list, EditKind::Other);
  return result;
}

// ---------------------------------------------------------------------------------------------
// Registers

VimInputHandler::Register VimInputHandler::fetchRegister(QChar name) const {
  if (name.isNull() || name == u'"')
    return m_regs.value(u'"');
  if (name == u'_')
    return {};
  if (name == u'+' || name == u'*') {
    Register reg;
    if (m_host) {
      const QString text = m_host->clipboardText(name == u'*');
      reg.text = text;
      reg.pieces = {text};
      reg.type = text.endsWith(u'\n') ? Register::Type::Line : Register::Type::Char;
    }
    return reg;
  }
  if (name == u'.') {
    Register reg;
    reg.text = m_lastInserted;
    reg.pieces = {m_lastInserted};
    return reg;
  }
  if (name == u':') {
    Register reg;
    reg.text = m_lastEx;
    reg.pieces = {m_lastEx};
    return reg;
  }
  if (name == u'/') {
    Register reg;
    reg.text = m_search.pattern;
    reg.pieces = {m_search.pattern};
    return reg;
  }
  return m_regs.value(lowerOf(name));
}

VimInputHandler::Register VimInputHandler::readRegister(QChar name) const { return fetchRegister(name); }

void VimInputHandler::setRegister(QChar name, const QString &text, Register::Type type) {
  Register reg;
  reg.text = text;
  reg.pieces = {text};
  reg.type = type;
  if (name.isNull() || name == u'"') {
    m_regs[u'"'] = reg;
    return;
  }
  if (name == u'_')
    return;
  if (name == u'+' || name == u'*') {
    if (m_host)
      m_host->setClipboardText(text, name == u'*');
    return;
  }
  m_regs[lowerOf(name)] = reg;
}

void VimInputHandler::storeRegister(
  const QString &regName, const QStringList &pieces, Register::Type type, bool isDelete, bool bigMotion
) {
  Register reg;
  reg.pieces = pieces;
  reg.type = type;
  reg.text = type == Register::Type::Line ? pieces.join(QString()) : pieces.join(u"\n"_s);
  const QChar name = regName.isEmpty() ? QChar() : regName[0];
  if (name == u'_')
    return;
  if (name.isLetter() && name.isUpper()) {
    Register existing = m_regs.value(name.toLower());
    if (!existing.isEmpty()) {
      if (existing.type == Register::Type::Line || type == Register::Type::Line) {
        if (existing.type != Register::Type::Line && !existing.text.endsWith(u'\n'))
          existing.text += u'\n';
        reg.type = Register::Type::Line;
      }
      reg.text = existing.text + reg.text;
      reg.pieces = existing.pieces + reg.pieces;
    }
    m_regs[name.toLower()] = reg;
    m_regs[u'"'] = reg;
    return;
  }
  if (name.isLetterOrNumber() && name.unicode() < 128) {
    m_regs[name] = reg;
    m_regs[u'"'] = reg;
    return;
  }
  if (name == u'+' || name == u'*') {
    if (m_host)
      m_host->setClipboardText(reg.text, name == u'*');
    m_regs[u'"'] = reg;
    return;
  }
  m_regs[u'"'] = reg;
  if (!isDelete) {
    m_regs[u'0'] = reg;
    return;
  }
  if (type == Register::Type::Line || reg.text.contains(u'\n') || bigMotion) {
    for (char16_t c = u'9'; c > u'1'; --c)
      if (m_regs.contains(QChar(c - 1)))
        m_regs[QChar(c)] = m_regs.value(QChar(c - 1));
    m_regs[u'1'] = reg;
  } else {
    m_regs[u'-'] = reg;
  }
}

void VimInputHandler::yankRanges(
  const QList<Range> &ranges, const QString &reg, bool isDelete, bool bigMotion, bool block
) {
  const Rope &r = rope();
  QStringList pieces;
  bool allLines = !ranges.isEmpty();
  for (const Range &rg : ranges) {
    QString text = r.toString(rg.start, rg.end);
    if (rg.linewise && !text.endsWith(u'\n'))
      text += u'\n';
    allLines &= rg.linewise;
    pieces.append(text);
  }
  const Register::Type type = allLines ? Register::Type::Line
                              : block  ? Register::Type::Block
                                       : Register::Type::Char;
  storeRegister(reg, pieces, type, isDelete, bigMotion);
}

// ---------------------------------------------------------------------------------------------
// Marks and jumps

void VimInputHandler::setMark(QChar name, qsizetype offset) {
  if (!m_doc)
    return;
  if (name == u'`')
    name = u'\'';
  const bool valid = (name.unicode() < 128 && name.isLetter()) || QStringView(u"'<>[].^").contains(name);
  if (!valid)
    return;
  AnchorSet &anchors = m_doc->anchors();
  const auto it = m_marks.constFind(name);
  if (it != m_marks.constEnd() && anchors.contains(it.value()))
    anchors.move(it.value(), offset);
  else
    m_marks[name] = anchors.create(offset, Gravity::Left);
}

qsizetype VimInputHandler::markOffset(QChar name) const {
  if (!m_doc)
    return -1;
  if (name == u'`')
    name = u'\'';
  const auto it = m_marks.constFind(name);
  if (it == m_marks.constEnd() || !m_doc->anchors().contains(it.value()))
    return -1;
  return m_doc->anchors().offset(it.value());
}

void VimInputHandler::pushJump(qsizetype offset) {
  AnchorSet &anchors = m_doc->anchors();
  const qsizetype line = rope().lineAt(offset);
  for (qsizetype i = m_jumps.size() - 1; i >= 0; --i) {
    const AnchorId id = m_jumps[i];
    if (!anchors.contains(id) || rope().lineAt(anchors.offset(id)) == line) {
      anchors.remove(id);
      m_jumps.removeAt(i);
    }
  }
  m_jumps.append(anchors.create(offset, Gravity::Left));
  while (m_jumps.size() > 100) {
    anchors.remove(m_jumps.first());
    m_jumps.removeFirst();
  }
  m_jumpIndex = int(m_jumps.size());
  setMark(u'\'', offset);
}

void VimInputHandler::jumpOlder(int count, bool older) {
  AnchorSet &anchors = m_doc->anchors();
  if (older) {
    if (m_jumpIndex >= m_jumps.size()) {
      pushJump(m_sel->primary().head);
      m_jumpIndex = int(m_jumps.size()) - 1;
    }
    const int target = m_jumpIndex - count;
    if (target < 0 || !anchors.contains(m_jumps[target])) {
      m_failed = true;
      return;
    }
    m_jumpIndex = target;
  } else {
    const int target = m_jumpIndex + count;
    if (target >= m_jumps.size() || !anchors.contains(m_jumps[target])) {
      m_failed = true;
      return;
    }
    m_jumpIndex = target;
  }
  setCursors({clampNormal(anchors.offset(m_jumps[m_jumpIndex]))}, 0);
}

// ---------------------------------------------------------------------------------------------
// Operators

bool VimInputHandler::applyOperator(const QString &op, QList<Range> ranges, const Cmd &cmd, bool block) {
  if (ranges.isEmpty())
    return false;
  std::stable_sort(ranges.begin(), ranges.end(), [](const Range &a, const Range &b) {
    return a.start < b.start;
  });
  QList<Range> merged;
  for (const Range &rg : ranges) {
    if (!merged.isEmpty() && rg.start < merged.last().end) {
      merged.last().end = qMax(merged.last().end, rg.end);
      merged.last().linewise |= rg.linewise;
    } else {
      merged.append(rg);
    }
  }
  const bool big = m_bigMotion;
  m_bigMotion = false;
  const QString reg = cmd.reg;
  m_cmdReg = reg;
  setMark(u'[', merged.first().start);
  setMark(u']', merged.last().end);
  if (op == u"y"_s) {
    yankRanges(merged, reg, false, false, block);
    QList<qsizetype> cursors;
    if (block) {
      cursors = {merged.first().start};
    } else {
      for (const Range &rg : merged)
        cursors.append(clampNormal(rg.cursor));
    }
    setCursors(cursors, block ? 0 : qMin(m_sel->primaryIndex(), int(cursors.size()) - 1));
    return true;
  }
  if (m_ctx->settings.readOnly || m_doc->isLoading())
    return false;
  if (op == u"d"_s) {
    yankRanges(merged, reg, true, big, block);
    deleteRanges(merged, false, block);
  } else if (op == u"c"_s) {
    yankRanges(merged, reg, true, big, block);
    deleteRanges(merged, true, block);
  } else if (op == u">"_s || op == u"<"_s) {
    shiftLines(merged, op == u">"_s, 1);
  } else {
    changeCase(merged, op);
  }
  return true;
}

void VimInputHandler::deleteRanges(const QList<Range> &ranges, bool enterInsert, bool block) {
  const Rope &r = rope();
  QList<Edit> edits;
  QList<bool> linewise;
  QList<qsizetype> fallback; // where a range that edits nothing leaves the cursor
  const qsizetype lastLine = r.lineCount() - 1;
  for (const Range &rg : ranges) {
    if (rg.linewise) {
      const qsizetype a = r.lineAt(rg.start);
      const qsizetype b = r.lineAt(rg.end > rg.start ? rg.end - 1 : rg.start);
      if (enterInsert) {
        // Change whole lines: they become one empty line that keeps the first line's indentation.
        edits.append({r.lineStart(a), r.lineEnd(b), indentOfLine(a)});
      } else if (b == lastLine && a > 0) {
        edits.append({r.lineEnd(a - 1), r.length(), {}});
      } else {
        edits.append({r.lineStart(a), b == lastLine ? r.length() : r.lineStart(b + 1), {}});
      }
    } else {
      edits.append({rg.start, rg.end, {}});
    }
    linewise.append(rg.linewise);
    fallback.append(rg.start);
  }
  bool real = false;
  for (const Edit &e : edits)
    real |= e.start != e.end || !e.text.isEmpty();
  QList<qsizetype> starts, ends;
  if (real) {
    // Empty edits stay out of the batch (they would be no-op replacements) but keep their cursor.
    QList<Edit> list;
    QList<int> index;
    for (int i = 0; i < edits.size(); ++i) {
      if (edits[i].start != edits[i].end || !edits[i].text.isEmpty()) {
        list.append(edits[i]);
        index.append(i);
      }
    }
    const EditResult res = edit(list);
    if (!res.ok)
      return;
    starts = fallback;
    ends = fallback;
    qsizetype shift = 0;
    int next = 0;
    for (int i = 0; i < edits.size(); ++i) {
      if (next < index.size() && index[next] == i) {
        starts[i] = res.starts[next];
        ends[i] = res.ends[next];
        shift = res.ends[next] - edits[i].end;
        ++next;
      } else {
        starts[i] = ends[i] = fallback[i] + shift;
      }
    }
  } else {
    starts = ends = fallback;
  }
  const Rope &after = rope();
  QList<qsizetype> cursors;
  if (enterInsert) {
    cursors = ends;
  } else {
    for (int i = 0; i < starts.size(); ++i) {
      if (linewise[i]) {
        const qsizetype line = qMin(after.lineAt(qMin(starts[i], after.length())), after.lineCount() - 1);
        cursors.append(firstNonBlank(after, line));
      } else {
        cursors.append(starts[i]);
      }
    }
  }
  if (block) {
    if (enterInsert) {
      // Rows too short to have anything in the block get no cursor.
      QList<qsizetype> rows;
      for (int i = 0; i < ranges.size(); ++i)
        if (ranges[i].end > ranges[i].start)
          rows.append(cursors[i]);
      if (!rows.isEmpty())
        cursors = rows;
    } else {
      cursors = {cursors.first()};
    }
  }
  if (!cursors.isEmpty())
    setCursors(cursors, block ? 0 : qMin(m_sel->primaryIndex(), int(cursors.size()) - 1));
  if (enterInsert) {
    startInsert(1);
    m_blockInsert = block && cursors.size() > 1;
    if (m_blockInsert)
      m_blockOrigin = cursors.first();
  }
}

void VimInputHandler::changeCase(const QList<Range> &ranges, const QString &op) {
  const Rope &r = rope();
  QList<Edit> edits;
  QList<qsizetype> cursorsBefore;
  for (const Range &rg : ranges) {
    const QString text = r.toString(rg.start, rg.end);
    const QString changed = op == u"gu"_s ? text.toLower() : op == u"gU"_s ? text.toUpper() : swapCase(text);
    if (changed != text)
      edits.append({rg.start, rg.end, changed});
    cursorsBefore.append(rg.cursor);
  }
  QList<qsizetype> cursors = cursorsBefore;
  if (!edits.isEmpty()) {
    const EditResult res = edit(edits);
    if (!res.ok)
      return;
    // Cursors before an edit stay; after one they shift by the length it changed.
    for (qsizetype &c : cursors) {
      qsizetype shift = 0;
      for (int i = 0; i < edits.size(); ++i)
        if (edits[i].end <= c)
          shift += edits[i].text.size() - (edits[i].end - edits[i].start);
      c += shift;
    }
  }
  setCursors(cursors, qMin(m_sel->primaryIndex(), int(cursors.size()) - 1));
}

void VimInputHandler::shiftLines(const QList<Range> &ranges, bool right, int amount) {
  const Rope &r = rope();
  const QString unit = indentUnit();
  const int width = qMax(1, m_ctx->settings.indentWidth);
  const int tw = tabWidth();
  QList<qsizetype> lines;
  QList<qsizetype> firstLines;
  for (const Range &rg : ranges) {
    qsizetype a = r.lineAt(rg.start);
    qsizetype b = r.lineAt(rg.end > rg.start ? rg.end - 1 : rg.start);
    if (!rg.linewise && rg.end > rg.start && rg.end == r.lineStart(r.lineAt(rg.end)))
      b = r.lineAt(rg.end) - 1;
    firstLines.append(a);
    for (qsizetype line = a; line <= b; ++line)
      if (lines.isEmpty() || lines.last() < line)
        lines.append(line);
  }
  QList<Edit> edits;
  for (qsizetype line : lines) {
    const qsizetype start = r.lineStart(line);
    if (right) {
      if (r.lineLength(line) == 0)
        continue;
      QString text;
      for (int i = 0; i < amount; ++i)
        text += unit;
      edits.append({start, start, text});
    } else {
      qsizetype p = start;
      const qsizetype end = firstNonBlank(r, line);
      int need = width * amount, column = 0;
      while (p < end && need > 0) {
        const int cell = r.at(p) == u'\t' ? tw - column % tw : 1;
        column += cell;
        need -= cell;
        ++p;
      }
      if (p > start)
        edits.append({start, p, {}});
    }
  }
  if (!edits.isEmpty()) {
    if (!edit(edits).ok)
      return;
  }
  const Rope &after = rope();
  QList<qsizetype> cursors;
  for (qsizetype line : firstLines)
    cursors.append(firstNonBlank(after, qMin(line, after.lineCount() - 1)));
  setCursors(cursors, qMin(m_sel->primaryIndex(), int(cursors.size()) - 1));
}

void VimInputHandler::joinLines(int count, bool spaces) {
  const Rope &r = rope();
  QList<Edit> edits;
  QList<qsizetype> cursorAt; // offset in the old text, as the position of the last join
  QList<qsizetype> cursorDelta;
  qsizetype previousEnd = 0;
  const qsizetype lastLine = r.lineCount() - 1;
  for (qsizetype head : heads()) {
    const qsizetype first = r.lineAt(head);
    if (first >= lastLine)
      continue;
    const qsizetype last = qMin<qsizetype>(first + count - 1, lastLine);
    const qsizetype start = r.lineEnd(first);
    if (start < previousEnd)
      continue;
    QString tail;
    bool endsBlank = r.lineLength(first) > 0 && (r.at(start - 1) == u' ' || r.at(start - 1) == u'\t');
    bool emptySoFar = r.lineLength(first) == 0;
    qsizetype cursorOffset = 0;
    for (qsizetype line = first + 1; line <= last; ++line) {
      const qsizetype from = spaces ? firstNonBlank(r, line) : r.lineStart(line);
      const QString next = r.toString(from, r.lineEnd(line));
      QString sep;
      if (spaces && !next.isEmpty() && !endsBlank && !emptySoFar && !next.startsWith(u')'))
        sep = u" "_s;
      cursorOffset = tail.size();
      tail += sep + next;
      if (!next.isEmpty()) {
        endsBlank = next.back() == u' ' || next.back() == u'\t';
        emptySoFar = false;
      }
    }
    edits.append({start, r.lineEnd(last), tail});
    cursorAt.append(start);
    cursorDelta.append(cursorOffset);
    previousEnd = r.lineEnd(last);
  }
  if (edits.isEmpty()) {
    m_failed = true;
    return;
  }
  const EditResult res = edit(edits);
  if (!res.ok)
    return;
  QList<qsizetype> cursors;
  for (int i = 0; i < res.starts.size(); ++i)
    cursors.append(res.starts[i] + cursorDelta[i]);
  setCursors(cursors, qMin(m_sel->primaryIndex(), int(cursors.size()) - 1));
}

void VimInputHandler::pasteRegister(QChar name, bool after, int count, bool cursorAfter) {
  if (m_ctx->settings.readOnly || m_doc->isLoading())
    return;
  const Register reg = fetchRegister(name);
  if (reg.isEmpty()) {
    setMessage(u"E353: Nothing in register "_s + (name.isNull() ? QString(u'"') : QString(name)));
    return;
  }
  const Rope &r = rope();
  TextBoundaries bounds(r);
  const QList<qsizetype> pos = heads();
  const bool distribute =
    pos.size() > 1 && reg.pieces.size() == pos.size() && reg.type != Register::Type::Block;

  if (reg.type == Register::Type::Block) {
    // Rows go into consecutive lines at the cursor's column; missing lines are appended.
    const qsizetype head = m_sel->primary().head;
    const int tw = tabWidth();
    const qsizetype line0 = r.lineAt(head);
    int column = virtualColumn(r, head, tw);
    if (after && head < r.lineEnd(line0))
      column += cellWidthAt(r, head, column, tw);
    int width = 0;
    for (const QString &piece : reg.pieces)
      width = qMax<int>(width, int(piece.size()));
    QList<Edit> edits;
    QString trailing;
    const qsizetype lastLine = r.lineCount() - 1;
    for (int row = 0; row < reg.pieces.size(); ++row) {
      QString piece;
      for (int c = 0; c < count; ++c)
        piece += reg.pieces[row].leftJustified(width, u' ');
      const qsizetype line = line0 + row;
      if (line <= lastLine) {
        bool past = false;
        qsizetype at = offsetAtVirtualColumn(r, line, column, tw, &past);
        QString text = piece;
        if (past) {
          const int have = virtualColumn(r, r.lineEnd(line), tw);
          text = QString(qMax(0, column - have), u' ') + reg.pieces[row].repeated(count);
        } else if (at >= r.lineEnd(line)) {
          text = reg.pieces[row].repeated(count);
        }
        edits.append({at, at, text});
      } else {
        trailing += u"\n"_s + QString(column, u' ') + reg.pieces[row].repeated(count);
      }
    }
    if (!trailing.isEmpty())
      edits.append({r.length(), r.length(), trailing});
    const EditResult res = edit(edits);
    if (res.ok && !res.starts.isEmpty())
      setCursors({res.starts.first()}, 0);
    return;
  }

  QList<Edit> edits;
  QList<qsizetype> cursorRel; // cursor offset relative to the replacement's start
  QList<bool> lineStartCursor;
  for (int i = 0; i < pos.size(); ++i) {
    const qsizetype head = pos[i];
    const qsizetype line = r.lineAt(head);
    QString text = distribute ? reg.pieces[i] : reg.text;
    if (reg.type == Register::Type::Line) {
      if (!text.endsWith(u'\n'))
        text += u'\n';
      const QString payload = text.repeated(count);
      if (after) {
        const FoldMap *folds = m_ctx->map && m_ctx->map->folds().hasFolds() ? &m_ctx->map->folds() : nullptr;
        const qsizetype below = folds ? folds->nextVisibleLine(line) : line + 1;
        if (below < r.lineCount()) {
          edits.append({r.lineStart(below), r.lineStart(below), payload});
          cursorRel.append(0);
        } else {
          const qsizetype at = r.length();
          edits.append({at, at, u"\n"_s + payload.left(payload.size() - 1)});
          cursorRel.append(1);
        }
      } else {
        edits.append({r.lineStart(line), r.lineStart(line), payload});
        cursorRel.append(0);
      }
      lineStartCursor.append(true);
    } else {
      const QString payload = text.repeated(count);
      qsizetype at = head;
      if (after && head < r.lineEnd(line))
        at = bounds.nextGrapheme(head);
      edits.append({at, at, payload});
      if (cursorAfter) {
        cursorRel.append(payload.size());
      } else if (payload.contains(u'\n')) {
        cursorRel.append(0);
      } else {
        // on the last pasted character
        cursorRel.append(
          payload.isEmpty() ? 0 : qsizetype(payload.size() - (payload.back().isLowSurrogate() ? 2 : 1))
        );
      }
      lineStartCursor.append(false);
    }
  }
  // Insertions at the same offset (several cursors on one line) would collide: keep one per offset.
  QList<Edit> unique;
  QList<qsizetype> uniqueRel;
  QList<bool> uniqueLine;
  for (int i = 0; i < edits.size(); ++i) {
    if (!unique.isEmpty() && unique.last().start == edits[i].start)
      continue;
    unique.append(edits[i]);
    uniqueRel.append(cursorRel[i]);
    uniqueLine.append(lineStartCursor[i]);
  }
  const EditResult res = edit(unique);
  if (!res.ok)
    return;
  const Rope &now = rope();
  QList<qsizetype> cursors;
  for (int i = 0; i < res.starts.size(); ++i) {
    qsizetype c = res.starts[i] + uniqueRel[i];
    if (uniqueLine[i] && !cursorAfter)
      c = firstNonBlank(now, now.lineAt(qMin(c, now.length())));
    else if (uniqueLine[i] && cursorAfter)
      c = res.ends[i] < now.length() ? res.ends[i] : lastCharOffset(now, now.lineCount() - 1);
    cursors.append(c);
  }
  setCursors(cursors, qMin(m_sel->primaryIndex(), int(cursors.size()) - 1));
}

void VimInputHandler::replaceSelectionWithRegister(QChar name, bool swapRegister) {
  const Register reg = fetchRegister(name);
  bool linewise = false, block = false;
  QList<Range> ranges = visualRanges(&linewise, &block);
  const Rope &r = rope();
  m_lastVisual = {m_mode, m_vis, m_toEol, true};
  m_vis.clear();
  setMode(Mode::Normal);
  if (reg.isEmpty() || ranges.isEmpty() || m_ctx->settings.readOnly)
    return;
  if (swapRegister)
    yankRanges(ranges, QString(), true, false, block);
  const bool distribute = ranges.size() > 1 && reg.pieces.size() == ranges.size();
  QList<Edit> edits;
  for (int i = 0; i < ranges.size(); ++i) {
    QString text = distribute ? reg.pieces[i] : reg.text;
    if (linewise) {
      if (!text.endsWith(u'\n'))
        text += u'\n';
      if (
        ranges[i].end >= r.length() && !r.toString(ranges[i].start, ranges[i].end).endsWith(u'\n') &&
        text.endsWith(u'\n')
      )
        text.chop(1);
    } else if (reg.type == Register::Type::Line && text.endsWith(u'\n')) {
      text.chop(1);
    }
    edits.append({ranges[i].start, ranges[i].end, text});
  }
  const EditResult res = edit(edits);
  if (!res.ok)
    return;
  const Rope &now = rope();
  QList<qsizetype> cursors;
  for (int i = 0; i < res.starts.size(); ++i) {
    if (linewise)
      cursors.append(firstNonBlank(now, now.lineAt(qMin(res.starts[i], now.length()))));
    else
      cursors.append(
        res.ends[i] > res.starts[i] ? TextBoundaries(now).previousGrapheme(res.ends[i]) : res.starts[i]
      );
  }
  setCursors(block ? QList<qsizetype>{cursors.first()} : cursors, 0);
}

void VimInputHandler::replaceChars(const QString &ch, int count) {
  const Rope &r = rope();
  TextBoundaries bounds(r);
  QString replacement = ch;
  const bool newline = ch == u"<CR>"_s;
  if (ch == u"<Tab>"_s)
    replacement = u"\t"_s;
  else if (!newline && !vim::isPrintableSymbol(ch))
    return;
  QList<Edit> edits;
  qsizetype previousEnd = 0;
  for (qsizetype head : heads()) {
    qsizetype end = head;
    const qsizetype lineEnd = r.lineEnd(r.lineAt(head));
    int taken = 0;
    while (taken < count && end < lineEnd) {
      end = bounds.nextGrapheme(end);
      ++taken;
    }
    if (taken < count) {
      m_failed = true;
      return; // not enough characters on a line: vim does nothing at all
    }
    if (head < previousEnd)
      continue;
    const QString text = newline ? u"\n"_s + indentOfLine(r.lineAt(head), head - r.lineStart(r.lineAt(head)))
                                 : replacement.repeated(count);
    edits.append({head, end, text});
    previousEnd = end;
  }
  const EditResult res = edit(edits);
  if (!res.ok)
    return;
  QList<qsizetype> cursors;
  for (int i = 0; i < res.starts.size(); ++i)
    cursors.append(newline ? res.ends[i] : res.ends[i] - replacement.size());
  setCursors(cursors, qMin(m_sel->primaryIndex(), int(cursors.size()) - 1));
}

void VimInputHandler::toggleCaseAtCursor(int count) {
  const Rope &r = rope();
  TextBoundaries bounds(r);
  QList<Edit> edits;
  QList<qsizetype> ends;
  qsizetype previousEnd = 0;
  for (qsizetype head : heads()) {
    const qsizetype lineEnd = r.lineEnd(r.lineAt(head));
    qsizetype end = head;
    for (int i = 0; i < count && end < lineEnd; ++i)
      end = bounds.nextGrapheme(end);
    if (head < previousEnd) {
      ends.append(end);
      continue;
    }
    if (end > head)
      edits.append({head, end, swapCase(r.toString(head, end))});
    ends.append(end);
    previousEnd = end;
  }
  if (!edits.isEmpty() && !edit(edits).ok)
    return;
  QList<qsizetype> cursors;
  for (qsizetype e : ends)
    cursors.append(clampNormal(e));
  setCursors(cursors, qMin(m_sel->primaryIndex(), int(cursors.size()) - 1));
}

void VimInputHandler::incrementNumber(int delta) {
  const Rope &r = rope();
  static const QRegularExpression number(QStringLiteral("-?\\d+"));
  QList<Edit> edits;
  QList<qsizetype> lastDigits; // offset in the new text relative to the edit's start
  qsizetype previousEnd = 0;
  for (qsizetype head : heads()) {
    const qsizetype line = r.lineAt(head);
    const qsizetype start = r.lineStart(line);
    const QString text = r.toString(start, r.lineEnd(line));
    const qsizetype col = head - start;
    QRegularExpressionMatchIterator it = number.globalMatch(text);
    bool found = false;
    while (it.hasNext()) {
      const QRegularExpressionMatch m = it.next();
      if (m.capturedEnd() <= col)
        continue;
      QString digits = m.captured();
      // A '-' that is part of a word ("a-1") is a separator rather than a sign.
      qsizetype from = m.capturedStart();
      if (
        digits.startsWith(u'-') && from > 0 && (text[from - 1].isLetterOrNumber() || text[from - 1] == u'_')
      )
        digits.remove(0, 1), ++from;
      bool ok = false;
      const qlonglong value = digits.toLongLong(&ok);
      if (!ok)
        break;
      QString out = QString::number(value + delta);
      const QString plain = digits.startsWith(u'-') ? digits.mid(1) : digits;
      if (plain.size() > 1 && plain.startsWith(u'0')) { // keep the width of 007
        QString body = QString::number(qAbs(value + delta));
        out = QString(value + delta < 0 ? u"-"_s : QString()) + body.rightJustified(int(plain.size()), u'0');
      }
      const qsizetype absStart = start + from, absEnd = start + m.capturedEnd();
      if (absStart < previousEnd)
        break;
      edits.append({absStart, absEnd, out});
      previousEnd = absEnd;
      found = true;
      break;
    }
    if (!found)
      continue;
  }
  if (edits.isEmpty()) {
    m_failed = true;
    return;
  }
  const EditResult res = edit(edits);
  if (!res.ok)
    return;
  QList<qsizetype> cursors;
  for (int i = 0; i < res.ends.size(); ++i)
    cursors.append(res.ends[i] - 1);
  setCursors(cursors, qMin(m_sel->primaryIndex(), int(cursors.size()) - 1));
}

} // namespace qce
