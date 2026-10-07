#include "core/vim/vimmotions.h"

#include <QtCore/QChar>

#include <algorithm>

namespace qce::vim {

namespace {

// The code point at `pos` (a surrogate pair is one) and how many units it takes.
char32_t codePointAt(const Rope &rope, qsizetype pos, int *units = nullptr) {
  const QChar c = rope.at(pos);
  if (c.isHighSurrogate() && pos + 1 < rope.length() && rope.at(pos + 1).isLowSurrogate()) {
    if (units)
      *units = 2;
    return QChar::surrogateToUcs4(c, rope.at(pos + 1));
  }
  if (units)
    *units = 1;
  return c.unicode();
}

qsizetype nextCP(const Rope &rope, qsizetype pos) {
  if (pos >= rope.length())
    return rope.length();
  int units = 1;
  codePointAt(rope, pos, &units);
  return pos + units;
}

qsizetype prevCP(const Rope &rope, qsizetype pos) {
  if (pos <= 0)
    return 0;
  const QChar c = rope.at(pos - 1);
  if (c.isLowSurrogate() && pos >= 2 && rope.at(pos - 2).isHighSurrogate())
    return pos - 2;
  return pos - 1;
}

bool isBlankCP(char32_t c) {
  return c == U' ' || c == U'\t' || (c != U'\n' && c != U'\r' && QChar::isSpace(c));
}

CharClass classOfCP(char32_t c, bool big) {
  if (isBlankCP(c))
    return CharClass::Blank;
  if (big)
    return CharClass::Word;
  if (c == U'_' || QChar::isLetterOrNumber(c))
    return CharClass::Word;
  const QChar::Category category = QChar::category(c);
  if (
    category == QChar::Mark_NonSpacing || category == QChar::Mark_SpacingCombining ||
    category == QChar::Mark_Enclosing
  )
    return CharClass::Word;
  return CharClass::Punct;
}

// One position forward, crossing line breaks: from a line's end to the next line's start. At the
// end of the document it stays.
qsizetype stepForward(const Rope &rope, qsizetype pos) {
  const qsizetype line = rope.lineAt(pos);
  const qsizetype end = rope.lineEnd(line);
  if (pos < end)
    return nextCP(rope, pos);
  if (line + 1 >= rope.lineCount())
    return pos;
  return rope.lineStart(line + 1);
}

// One position back, crossing line breaks: from a line's start to the previous line's end. -1 at the
// start of the document.
qsizetype stepBack(const Rope &rope, qsizetype pos) {
  const qsizetype line = rope.lineAt(pos);
  if (pos > rope.lineStart(line))
    return prevCP(rope, pos);
  if (line == 0)
    return -1;
  return rope.lineEnd(line - 1);
}

bool isBlankLine(const Rope &rope, qsizetype line) { return rope.lineLength(line) == 0; }

} // namespace

CharClass classAt(const Rope &rope, qsizetype pos, bool big) {
  const qsizetype length = rope.length();
  pos = qBound<qsizetype>(0, pos, length);
  const qsizetype line = rope.lineAt(pos);
  const qsizetype end = rope.lineEnd(line);
  if (pos >= end)
    return rope.lineStart(line) == end ? CharClass::Empty : CharClass::Newline;
  return classOfCP(codePointAt(rope, pos), big);
}

qsizetype wordForward(const Rope &rope, qsizetype pos, int count, bool big) {
  const qsizetype length = rope.length();
  pos = qBound<qsizetype>(0, pos, length);
  for (int n = 0; n < count && pos < length; ++n) {
    qsizetype line = rope.lineAt(pos);
    qsizetype end = rope.lineEnd(line);
    // Leave the current word.
    if (pos < end) {
      const CharClass cls = classAt(rope, pos, big);
      if (cls != CharClass::Blank) {
        while (pos < end && classAt(rope, pos, big) == cls)
          pos = nextCP(rope, pos);
      }
    }
    // Skip blanks and line breaks up to the next word; an empty line is one.
    for (;;) {
      while (pos < end && classAt(rope, pos, big) == CharClass::Blank)
        pos = nextCP(rope, pos);
      if (pos < end)
        break;
      if (line + 1 >= rope.lineCount()) {
        pos = length;
        break;
      }
      ++line;
      pos = rope.lineStart(line);
      end = rope.lineEnd(line);
      if (pos == end)
        break;
    }
  }
  return pos;
}

qsizetype wordBackward(const Rope &rope, qsizetype pos, int count, bool big) {
  pos = qBound<qsizetype>(0, pos, rope.length());
  for (int n = 0; n < count && pos > 0; ++n) {
    qsizetype p = stepBack(rope, pos);
    if (p < 0)
      return 0;
    CharClass cls;
    for (;;) {
      cls = classAt(rope, p, big);
      if (cls == CharClass::Empty)
        break;
      if (cls != CharClass::Blank && cls != CharClass::Newline)
        break;
      const qsizetype q = stepBack(rope, p);
      if (q < 0) {
        p = 0;
        break;
      }
      p = q;
    }
    if (cls == CharClass::Word || cls == CharClass::Punct) {
      const qsizetype start = rope.lineStart(rope.lineAt(p));
      while (p > start && classAt(rope, prevCP(rope, p), big) == cls)
        p = prevCP(rope, p);
    }
    pos = p;
  }
  return pos;
}

qsizetype wordEnd(const Rope &rope, qsizetype pos, int count, bool big) {
  const qsizetype length = rope.length();
  pos = qBound<qsizetype>(0, pos, length);
  for (int n = 0; n < count; ++n) {
    qsizetype p = stepForward(rope, pos);
    if (p == pos)
      break; // end of the document
    CharClass cls = classAt(rope, p, big);
    while (cls == CharClass::Blank || cls == CharClass::Newline || cls == CharClass::Empty) {
      const qsizetype q = stepForward(rope, p);
      if (q == p) {
        // Nothing but blanks until the end: stay on the last character.
        return qMax<qsizetype>(pos, prevCP(rope, length));
      }
      p = q;
      cls = classAt(rope, p, big);
    }
    const qsizetype end = rope.lineEnd(rope.lineAt(p));
    for (qsizetype q = nextCP(rope, p); q < end && classAt(rope, q, big) == cls; q = nextCP(rope, q))
      p = q;
    pos = p;
  }
  return pos;
}

qsizetype wordEndBackward(const Rope &rope, qsizetype pos, int count, bool big) {
  pos = qBound<qsizetype>(0, pos, rope.length());
  for (int n = 0; n < count && pos > 0; ++n) {
    qsizetype p = pos;
    const CharClass cls = classAt(rope, p, big);
    if (cls == CharClass::Word || cls == CharClass::Punct) {
      const qsizetype start = rope.lineStart(rope.lineAt(p));
      while (p > start && classAt(rope, prevCP(rope, p), big) == cls)
        p = prevCP(rope, p);
    }
    p = stepBack(rope, p);
    if (p < 0)
      return 0;
    for (;;) {
      const CharClass c = classAt(rope, p, big);
      if (c == CharClass::Empty || (c != CharClass::Blank && c != CharClass::Newline))
        break;
      const qsizetype q = stepBack(rope, p);
      if (q < 0)
        return 0;
      p = q;
    }
    pos = p;
  }
  return pos;
}

qsizetype firstNonBlank(const Rope &rope, qsizetype line) {
  qsizetype p = rope.lineStart(line);
  const qsizetype end = rope.lineEnd(line);
  while (p < end && isBlankCP(rope.at(p).unicode()))
    ++p;
  return p;
}

qsizetype lastNonBlank(const Rope &rope, qsizetype line) {
  const qsizetype start = rope.lineStart(line);
  qsizetype p = rope.lineEnd(line);
  while (p > start && isBlankCP(rope.at(p - 1).unicode()))
    --p;
  return p > start ? prevCP(rope, p) : start;
}

qsizetype findCharInLine(
  const Rope &rope, qsizetype pos, QStringView ch, int count, bool forward, bool till, bool skipAdjacent
) {
  if (ch.isEmpty())
    return -1;
  const qsizetype line = rope.lineAt(pos);
  const qsizetype start = rope.lineStart(line), end = rope.lineEnd(line);
  constexpr qsizetype kWindow = 4096;
  qsizetype found = -1;
  int remaining = count;
  if (forward) {
    qsizetype from = nextCP(rope, pos);
    if (skipAdjacent && till && from < end && rope.toString(from, qMin(end, from + ch.size())) == ch)
      from = nextCP(rope, from);
    while (from < end) {
      const qsizetype to = qMin(end, from + kWindow);
      const QString window = rope.toString(from, to);
      qsizetype index = 0;
      while ((index = window.indexOf(ch, index)) >= 0) {
        if (--remaining == 0) {
          found = from + index;
          break;
        }
        index += ch.size();
      }
      if (found >= 0)
        break;
      from = to - (ch.size() - 1);
      if (to == end)
        break;
    }
    if (found < 0)
      return -1;
    return till ? prevCP(rope, found) : found;
  }
  qsizetype to = pos;
  if (skipAdjacent && till && to > start && rope.toString(qMax(start, to - ch.size()), to) == ch)
    to = prevCP(rope, to);
  while (to > start) {
    const qsizetype from = qMax(start, to - kWindow);
    const QString window = rope.toString(from, to);
    qsizetype index = window.size();
    while (index > 0 && (index = window.lastIndexOf(ch, index - 1)) >= 0) {
      if (--remaining == 0) {
        found = from + index;
        break;
      }
      if (index == 0)
        break;
    }
    if (found >= 0)
      break;
    to = from + (ch.size() - 1);
    if (from == start)
      break;
  }
  if (found < 0)
    return -1;
  return till ? nextCP(rope, found) : found;
}

qsizetype paragraphForward(const Rope &rope, qsizetype pos, int count) {
  qsizetype line = rope.lineAt(pos);
  const qsizetype start = line;
  const qsizetype last = rope.lineCount() - 1;
  for (int n = 0; n < count; ++n) {
    while (line <= last && isBlankLine(rope, line))
      ++line;
    while (line <= last && !isBlankLine(rope, line)) {
      ++line;
      if (line - start > kParagraphScanLines)
        return pos;
    }
    if (line > last)
      return rope.length();
  }
  return rope.lineStart(line);
}

qsizetype paragraphBackward(const Rope &rope, qsizetype pos, int count) {
  qsizetype line = rope.lineAt(pos);
  const qsizetype start = line;
  for (int n = 0; n < count; ++n) {
    // Starting in the middle of a paragraph, the first line stays; at its first character we go on.
    while (line >= 0 && isBlankLine(rope, line))
      --line;
    while (line >= 0 && !isBlankLine(rope, line)) {
      --line;
      if (start - line > kParagraphScanLines)
        return pos;
    }
    if (line < 0)
      return 0;
  }
  return rope.lineStart(line);
}

namespace {

bool isSentenceCloser(QChar c) { return c == u')' || c == u']' || c == u'"' || c == u'\''; }

} // namespace

bool isSentenceStart(const Rope &rope, qsizetype pos) {
  pos = qBound<qsizetype>(0, pos, rope.length());
  const CharClass cls = classAt(rope, pos);
  if (cls == CharClass::Empty)
    return true;
  if (cls != CharClass::Word && cls != CharClass::Punct)
    return false;
  const qsizetype line = rope.lineAt(pos);
  const qsizetype start = rope.lineStart(line);
  qsizetype q = pos;
  bool blanks = false;
  while (q > start && isBlankCP(rope.at(q - 1).unicode())) {
    --q;
    blanks = true;
  }
  auto endsSentenceBefore = [&](qsizetype at, qsizetype lineStart) {
    // `at` is just past the last non-blank of the stretch before; look at what ends it.
    while (at > lineStart && isSentenceCloser(rope.at(at - 1)))
      --at;
    if (at <= lineStart)
      return false;
    const QChar c = rope.at(at - 1);
    return c == u'.' || c == u'!' || c == u'?';
  };
  if (q > start) {
    if (!blanks)
      return false;
    return endsSentenceBefore(q, start);
  }
  // First non-blank of the line: the line before decides.
  if (line == 0)
    return true;
  if (isBlankLine(rope, line - 1))
    return true;
  qsizetype e = rope.lineEnd(line - 1);
  const qsizetype prevStart = rope.lineStart(line - 1);
  while (e > prevStart && isBlankCP(rope.at(e - 1).unicode()))
    --e;
  return endsSentenceBefore(e, prevStart);
}

qsizetype sentenceForward(const Rope &rope, qsizetype pos, int count) {
  const qsizetype length = rope.length();
  pos = qBound<qsizetype>(0, pos, length);
  for (int n = 0; n < count && pos < length; ++n) {
    qsizetype p = stepForward(rope, pos);
    const qsizetype limit = qMin(length, pos + kMotionScanLimit);
    while (p < limit && !isSentenceStart(rope, p)) {
      const qsizetype q = stepForward(rope, p);
      if (q == p) {
        p = length;
        break;
      }
      p = q;
    }
    if (p >= limit && p < length && !isSentenceStart(rope, p))
      return pos; // no sentence start within reach
    pos = qMin(p, length);
  }
  return pos;
}

qsizetype sentenceBackward(const Rope &rope, qsizetype pos, int count) {
  pos = qBound<qsizetype>(0, pos, rope.length());
  for (int n = 0; n < count && pos > 0; ++n) {
    qsizetype p = stepBack(rope, pos);
    const qsizetype limit = qMax<qsizetype>(0, pos - kMotionScanLimit);
    while (p > limit && !isSentenceStart(rope, p)) {
      const qsizetype q = stepBack(rope, p);
      if (q < 0) {
        p = 0;
        break;
      }
      p = q;
    }
    if (p <= limit && p > 0 && !isSentenceStart(rope, p))
      return pos;
    pos = qMax<qsizetype>(p, 0);
  }
  return pos;
}

int virtualColumn(const Rope &rope, qsizetype offset, int tabWidth) {
  const qsizetype line = rope.lineAt(offset);
  const qsizetype start = rope.lineStart(line);
  offset = qMin(offset, rope.lineEnd(line));
  int column = 0;
  constexpr qsizetype kWindow = 4096;
  for (qsizetype from = start; from < offset; from += kWindow) {
    const QString window = rope.toString(from, qMin(offset, from + kWindow));
    for (QChar c : window) {
      if (c == u'\t')
        column += tabWidth - column % tabWidth;
      else if (!c.isLowSurrogate())
        ++column;
    }
  }
  return column;
}

int cellWidthAt(const Rope &rope, qsizetype offset, int column, int tabWidth) {
  if (offset < rope.length() && rope.at(offset) == u'\t')
    return tabWidth - column % tabWidth;
  return 1;
}

qsizetype offsetAtVirtualColumn(const Rope &rope, qsizetype line, int column, int tabWidth, bool *pastEnd) {
  const qsizetype start = rope.lineStart(line), end = rope.lineEnd(line);
  int col = 0;
  qsizetype p = start;
  constexpr qsizetype kWindow = 4096;
  while (p < end) {
    const QString window = rope.toString(p, qMin(end, p + kWindow));
    qsizetype i = 0;
    while (i < window.size()) {
      const int width = window[i] == u'\t' ? tabWidth - col % tabWidth : 1;
      if (column < col + width) {
        if (pastEnd)
          *pastEnd = false;
        return p + i;
      }
      col += width;
      i += window[i].isHighSurrogate() && i + 1 < window.size() ? 2 : 1;
    }
    p += i;
  }
  if (pastEnd)
    *pastEnd = true;
  return end;
}

bool isAtLineEnd(const Rope &rope, qsizetype offset) {
  const qsizetype line = rope.lineAt(offset);
  return offset >= lastCharOffset(rope, line);
}

qsizetype lastCharOffset(const Rope &rope, qsizetype line) {
  const qsizetype start = rope.lineStart(line), end = rope.lineEnd(line);
  return end > start ? prevCP(rope, end) : start;
}

} // namespace qce::vim
