#include "core/vim/vimtextobjects.h"

#include "core/bracketmatch.h"
#include "core/vim/vimmotions.h"

#include <QtCore/QRegularExpression>

#include <algorithm>
#include <vector>

using namespace Qt::StringLiterals;

namespace qce::vim {

namespace {

constexpr qsizetype kTagWindow = 200000;

// The run of same-class text around `pos` within its line: [a, b).
void runAround(const Rope &rope, qsizetype pos, bool big, qsizetype *a, qsizetype *b) {
  const qsizetype line = rope.lineAt(pos);
  const qsizetype start = rope.lineStart(line), end = rope.lineEnd(line);
  *a = *b = pos;
  if (pos >= end)
    return;
  const CharClass cls = classAt(rope, pos, big);
  while (*a > start && classAt(rope, *a - 1, big) == cls)
    --*a;
  while (*b < end && classAt(rope, *b, big) == cls)
    ++*b;
}

ObjectRange wordObject(
  const Rope &rope, qsizetype pos, bool around, bool big, int count, qsizetype selStart, qsizetype selEnd
) {
  const qsizetype line = rope.lineAt(pos);
  const qsizetype lineEndOff = rope.lineEnd(line), lineStartOff = rope.lineStart(line);
  if (lineEndOff == lineStartOff)
    return {true, pos, pos, false}; // an empty line: nothing to select
  qsizetype a, b;
  const bool extending = selEnd > selStart;
  if (extending) {
    // Grow from the end of the selection: the next run (and for `a` its trailing blanks).
    a = selStart;
    b = selEnd;
    if (b >= lineEndOff)
      return {};
  } else {
    runAround(rope, pos, big, &a, &b);
  }
  auto isBlankRun = [&](qsizetype at) {
    return at < lineEndOff && classAt(rope, at, big) == CharClass::Blank;
  };
  if (!around) {
    int n = extending ? count : count - 1;
    if (extending) {
      qsizetype ra, rb;
      runAround(rope, b, big, &ra, &rb);
      b = rb;
      --n;
    }
    for (; n > 0 && b < lineEndOff; --n) {
      qsizetype ra, rb;
      runAround(rope, b, big, &ra, &rb);
      b = rb;
    }
    return {true, a, b, false};
  }
  // `aw`: a word and the blanks after it; the blanks before it when nothing follows.
  const bool startedOnBlank = !extending && classAt(rope, pos, big) == CharClass::Blank;
  bool trailing = false;
  int n = count;
  if (startedOnBlank) {
    // Blanks then the word after them.
    if (b < lineEndOff) {
      qsizetype ra, rb;
      runAround(rope, b, big, &ra, &rb);
      b = rb;
    }
    --n;
    trailing = true;
  } else if (!extending) {
    if (isBlankRun(b)) {
      qsizetype ra, rb;
      runAround(rope, b, big, &ra, &rb);
      b = rb;
      trailing = true;
    }
    --n;
  }
  for (; n > 0 && b < lineEndOff; --n) {
    qsizetype ra, rb;
    runAround(rope, b, big, &ra, &rb);
    b = rb;
    trailing = false;
    if (isBlankRun(b)) {
      runAround(rope, b, big, &ra, &rb);
      b = rb;
      trailing = true;
    }
  }
  if (!trailing && !startedOnBlank && !extending) {
    qsizetype ra, rb;
    if (a > lineStartOff && classAt(rope, a - 1, big) == CharClass::Blank) {
      runAround(rope, a - 1, big, &ra, &rb);
      a = ra;
    }
  }
  return {true, a, b, false};
}

bool isBlankOnlyLine(const Rope &rope, qsizetype line) {
  return firstNonBlank(rope, line) >= rope.lineEnd(line);
}

ObjectRange paragraphObject(
  const Rope &rope, qsizetype pos, bool around, int count, qsizetype selStart, qsizetype selEnd
) {
  const qsizetype lines = rope.lineCount();
  qsizetype first = rope.lineAt(pos), last = first;
  const bool extending = selEnd > selStart;
  auto finish = [&](qsizetype f, qsizetype l) -> ObjectRange {
    return {true, rope.lineStart(f), l + 1 < lines ? rope.lineStart(l + 1) : rope.length(), true};
  };
  // Extends over the run of lines of the same kind (blank or not) as `line`.
  auto runEnd = [&](qsizetype line) {
    const bool blank = isBlankOnlyLine(rope, line);
    while (line + 1 < lines && isBlankOnlyLine(rope, line + 1) == blank)
      ++line;
    return line;
  };
  if (extending) {
    first = rope.lineAt(selStart);
    last = rope.lineAt(qMax(selStart, selEnd - 1));
    if (last + 1 >= lines)
      return {};
    qsizetype l = last + 1;
    for (int n = count; n > 0 && l < lines; --n) {
      last = runEnd(l);
      l = last + 1;
      if (around && l < lines && n == 1) {
        last = runEnd(l);
      }
    }
    return finish(first, last);
  }
  const bool onBlank = isBlankOnlyLine(rope, first);
  while (first > 0 && isBlankOnlyLine(rope, first - 1) == onBlank)
    --first;
  last = runEnd(rope.lineAt(pos));
  if (!around) {
    for (int n = count - 1; n > 0 && last + 1 < lines; --n)
      last = runEnd(last + 1);
    return finish(first, last);
  }
  // `ap`: the paragraph and the blank lines after it (or before it when nothing follows).
  bool trailing = false;
  int n = count;
  if (onBlank) {
    if (last + 1 < lines) {
      last = runEnd(last + 1);
      trailing = true;
    }
  } else if (last + 1 < lines) {
    last = runEnd(last + 1);
    trailing = true;
  }
  --n;
  for (; n > 0 && last + 1 < lines; --n) {
    last = runEnd(last + 1);
    trailing = false;
    if (last + 1 < lines) {
      last = runEnd(last + 1);
      trailing = true;
    }
  }
  if (!trailing && !onBlank && first > 0)
    while (first > 0 && isBlankOnlyLine(rope, first - 1))
      --first;
  return finish(first, last);
}

ObjectRange sentenceObject(const Rope &rope, qsizetype pos, bool around, int count) {
  qsizetype s = pos;
  qsizetype guard = 0;
  while (s > 0 && !isSentenceStart(rope, s) && guard++ < kMotionScanLimit)
    --s;
  qsizetype next = sentenceForward(rope, pos, count);
  qsizetype t = next;
  while (t > s && (classAt(rope, t - 1) == CharClass::Blank || classAt(rope, t - 1) == CharClass::Newline ||
                   rope.at(t - 1) == u'\n' || rope.at(t - 1) == u'\r'))
    --t;
  if (!around)
    return {true, s, t, false};
  if (next > t)
    return {true, s, next, false};
  // No trailing blanks: take the ones before the sentence.
  qsizetype a = s;
  while (a > 0 && classAt(rope, a - 1) == CharClass::Blank)
    --a;
  return {true, a, t, false};
}

ObjectRange quoteObject(const Rope &rope, qsizetype pos, QChar quote, bool around) {
  const qsizetype line = rope.lineAt(pos);
  const qsizetype start = rope.lineStart(line), end = rope.lineEnd(line);
  if (end - start > 100000)
    return {};
  const QString text = rope.toString(start, end);
  std::vector<qsizetype> quotes;
  for (qsizetype i = 0; i < text.size(); ++i) {
    if (text[i] == u'\\') {
      ++i;
      continue;
    }
    if (text[i] == quote)
      quotes.push_back(i);
  }
  const qsizetype col = pos - start;
  for (size_t k = 0; k + 1 < quotes.size(); k += 2) {
    const qsizetype open = quotes[k], close = quotes[k + 1];
    if (close < col)
      continue;
    if (!around)
      return {true, start + open + 1, start + close, false};
    qsizetype a = open, b = close + 1;
    if (b < text.size() && (text[b] == u' ' || text[b] == u'\t')) {
      while (b < text.size() && (text[b] == u' ' || text[b] == u'\t'))
        ++b;
    } else {
      while (a > 0 && (text[a - 1] == u' ' || text[a - 1] == u'\t'))
        --a;
    }
    return {true, start + a, start + b, false};
  }
  return {};
}

ObjectRange bracketObject(
  const Rope &rope, qsizetype pos, char16_t open, char16_t close, bool around, int count, qsizetype selStart,
  qsizetype selEnd
) {
  const BracketPairs pairs = {{open, close}};
  BracketPair pair;
  const bool extending = selEnd > selStart;
  if (extending) {
    // Widen: if the selection already is the inside (or all) of a pair, look outward from it.
    pair = findEnclosingBrackets(rope, selStart, pairs);
    if (
      pair.valid() && (around ? pair.open >= selStart : pair.open + 1 >= selStart) &&
      (around ? pair.close + 1 <= selEnd : pair.close <= selEnd)
    ) {
      pair = findEnclosingBrackets(rope, pair.open, pairs);
    } else if (pair.valid() && !around && pair.open + 1 == selStart && pair.close == selEnd) {
      pair = findEnclosingBrackets(rope, pair.open, pairs);
    }
    for (int n = count - 1; n > 0 && pair.valid(); --n)
      pair = findEnclosingBrackets(rope, pair.open, pairs);
  } else {
    const QChar here = pos < rope.length() ? rope.at(pos) : QChar();
    if (here == QChar(open) || here == QChar(close)) {
      pair = findMatchingBracket(rope, pos, pairs);
      if (pair.valid() && pair.open > pair.close) // on the closer: the pair as (opener, closer)
        pair = {pair.close, pair.open};
    } else {
      pair = findEnclosingBrackets(rope, pos, pairs);
    }
    for (int n = count - 1; n > 0 && pair.valid(); --n)
      pair = findEnclosingBrackets(rope, pair.open, pairs);
  }
  if (!pair.valid())
    return {};
  if (around)
    return {true, pair.open, pair.close + 1, false};
  // Inside. A block whose braces sit on lines of their own gives the lines between them.
  const qsizetype openLine = rope.lineAt(pair.open), closeLine = rope.lineAt(pair.close);
  if (closeLine > openLine && pair.open + 1 >= rope.lineEnd(openLine)) {
    const qsizetype before = rope.lineStart(closeLine);
    if (firstNonBlank(rope, closeLine) == pair.close && openLine + 1 <= closeLine - 1 + 1)
      return {true, rope.lineStart(openLine + 1), before, true};
  }
  return {true, pair.open + 1, pair.close, false};
}

struct Tag {
  qsizetype start, end; // the whole tag
  QString name;
  bool closing;
};

ObjectRange tagObject(const Rope &rope, qsizetype pos, bool around, int count) {
  const qsizetype from = qMax<qsizetype>(0, pos - kTagWindow);
  const qsizetype to = qMin(rope.length(), pos + kTagWindow);
  const QString text = rope.toString(from, to);
  static const QRegularExpression re(QStringLiteral("<(/?)([A-Za-z][A-Za-z0-9:_.-]*)(?:\\s[^<>]*?)?(/?)>"));
  static const QStringList voids = {u"br"_s,   u"hr"_s,   u"img"_s,  u"input"_s, u"meta"_s,
                                    u"link"_s, u"area"_s, u"base"_s, u"col"_s,   u"wbr"_s};
  struct Pair {
    Tag open, close;
  };
  std::vector<Pair> pairs;
  std::vector<Tag> stack;
  QRegularExpressionMatchIterator it = re.globalMatch(text);
  while (it.hasNext()) {
    const QRegularExpressionMatch m = it.next();
    if (!m.captured(3).isEmpty())
      continue; // self-closing
    Tag tag{from + m.capturedStart(), from + m.capturedEnd(), m.captured(2), !m.captured(1).isEmpty()};
    if (!tag.closing) {
      if (voids.contains(tag.name, Qt::CaseInsensitive))
        continue;
      stack.push_back(tag);
    } else {
      for (size_t k = stack.size(); k-- > 0;) {
        if (stack[k].name.compare(tag.name, Qt::CaseInsensitive) == 0) {
          pairs.push_back({stack[k], tag});
          stack.resize(k);
          break;
        }
      }
    }
  }
  // Pairs that enclose pos, innermost first (they were completed inner before outer).
  std::vector<const Pair *> enclosing;
  for (const Pair &p : pairs)
    if (p.open.start <= pos && pos < p.close.end)
      enclosing.push_back(&p);
  if (enclosing.empty())
    return {};
  std::sort(enclosing.begin(), enclosing.end(), [](const Pair *a, const Pair *b) {
    return a->open.start > b->open.start;
  });
  const size_t index = size_t(qMin<int>(count, int(enclosing.size())) - 1);
  const Pair &p = *enclosing[index];
  if (around)
    return {true, p.open.start, p.close.end, false};
  return {true, p.open.end, p.close.start, false};
}

} // namespace

ObjectRange textObject(
  const Rope &rope, qsizetype pos, QChar kind, bool around, int count, qsizetype selStart, qsizetype selEnd
) {
  pos = qBound<qsizetype>(0, pos, rope.length());
  count = qMax(1, count);
  switch (kind.unicode()) {
  case 'w':
    return wordObject(rope, pos, around, false, count, selStart, selEnd);
  case 'W':
    return wordObject(rope, pos, around, true, count, selStart, selEnd);
  case 's':
    return sentenceObject(rope, pos, around, count);
  case 'p':
    return paragraphObject(rope, pos, around, count, selStart, selEnd);
  case '"':
  case '\'':
  case '`':
    return quoteObject(rope, pos, kind, around);
  case '(':
  case ')':
  case 'b':
    return bracketObject(rope, pos, u'(', u')', around, count, selStart, selEnd);
  case '[':
  case ']':
    return bracketObject(rope, pos, u'[', u']', around, count, selStart, selEnd);
  case '{':
  case '}':
  case 'B':
    return bracketObject(rope, pos, u'{', u'}', around, count, selStart, selEnd);
  case '<':
  case '>':
    return bracketObject(rope, pos, u'<', u'>', around, count, selStart, selEnd);
  case 't':
    return tagObject(rope, pos, around, count);
  default:
    return {};
  }
}

} // namespace qce::vim
