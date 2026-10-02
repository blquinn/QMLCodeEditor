#include "core/textboundaries.h"

#include <QtCore/QTextBoundaryFinder>

namespace qce {

namespace {

constexpr qsizetype kReaderWindow = 4096;
constexpr qsizetype kGraphemeWindow = 256; // each side of the offset, widened when needed
constexpr qsizetype kGraphemeMargin = 64;  // a boundary this close to a cut edge is not trusted

// Random access to a rope through a cached window, so scans don't pay O(log n) per unit.
class Reader {
public:
  explicit Reader(const Rope &rope) : m_rope(rope), m_length(rope.length()) {}

  qsizetype length() const { return m_length; }

  QChar at(qsizetype i) {
    if (i < m_start || i >= m_start + m_window.size()) {
      m_start = qMax<qsizetype>(0, i - kReaderWindow / 2);
      m_window = m_rope.toString(m_start, qMin(m_length, m_start + kReaderWindow));
    }
    return m_window.at(i - m_start);
  }

  // Code point starting at `i` and its length in units.
  char32_t codePointAt(qsizetype i, int *len) {
    const QChar c = at(i);
    if (c.isHighSurrogate() && i + 1 < m_length && at(i + 1).isLowSurrogate()) {
      *len = 2;
      return QChar::surrogateToUcs4(c, at(i + 1));
    }
    *len = 1;
    return c.unicode();
  }

  // Code point ending at `i` (i > 0) and its length.
  char32_t codePointBefore(qsizetype i, int *len) {
    const QChar c = at(i - 1);
    if (c.isLowSurrogate() && i >= 2 && at(i - 2).isHighSurrogate()) {
      *len = 2;
      return QChar::surrogateToUcs4(at(i - 2), c);
    }
    *len = 1;
    return c.unicode();
  }

private:
  Rope m_rope;
  qsizetype m_length;
  qsizetype m_start = 0;
  QString m_window;
};

enum class Cls { Space, Word, Punct };

Cls classify(char32_t cp, bool bigWord) {
  if (QChar::isSpace(cp))
    return Cls::Space;
  if (bigWord)
    return Cls::Word;
  if (cp == U'_')
    return Cls::Word;
  switch (QChar::category(cp)) {
  case QChar::Letter_Uppercase:
  case QChar::Letter_Lowercase:
  case QChar::Letter_Titlecase:
  case QChar::Letter_Modifier:
  case QChar::Letter_Other:
  case QChar::Number_DecimalDigit:
  case QChar::Number_Letter:
  case QChar::Number_Other:
  case QChar::Mark_NonSpacing:
  case QChar::Mark_SpacingCombining:
  case QChar::Mark_Enclosing:
    return Cls::Word;
  default:
    return Cls::Punct;
  }
}

// Class of the code point starting at i / ending at i.
Cls classAfter(Reader &r, qsizetype i, bool big) {
  int len;
  const char32_t cp = r.codePointAt(i, &len);
  return classify(cp, big);
}

Cls classBefore(Reader &r, qsizetype i, bool big) {
  int len;
  const char32_t cp = r.codePointBefore(i, &len);
  return classify(cp, big);
}

qsizetype stepForward(Reader &r, qsizetype i) {
  int len;
  r.codePointAt(i, &len);
  return i + len;
}

qsizetype stepBack(Reader &r, qsizetype i) {
  int len;
  r.codePointBefore(i, &len);
  return i - len;
}

} // namespace

qsizetype TextBoundaries::nextCodePoint(qsizetype offset) const {
  offset = qBound<qsizetype>(0, offset, m_rope.length());
  if (offset >= m_rope.length())
    return m_rope.length();
  const QChar c = m_rope.at(offset);
  if (c.isHighSurrogate() && offset + 1 < m_rope.length() && m_rope.at(offset + 1).isLowSurrogate())
    return offset + 2;
  return offset + 1;
}

qsizetype TextBoundaries::previousCodePoint(qsizetype offset) const {
  offset = qBound<qsizetype>(0, offset, m_rope.length());
  if (offset <= 0)
    return 0;
  const QChar c = m_rope.at(offset - 1);
  if (c.isLowSurrogate() && offset >= 2 && m_rope.at(offset - 2).isHighSurrogate())
    return offset - 2;
  return offset - 1;
}

qsizetype TextBoundaries::nextGrapheme(qsizetype offset) const {
  const qsizetype len = m_rope.length();
  offset = qBound<qsizetype>(0, offset, len);
  if (offset >= len)
    return len;

  // ASCII followed by ASCII (or the end) is a boundary, except between CR and LF.
  const QChar c0 = m_rope.at(offset);
  if (c0.unicode() < 0x80) {
    if (offset + 1 == len)
      return len;
    const QChar c1 = m_rope.at(offset + 1);
    if (c1.unicode() < 0x80 && !(c0 == QLatin1Char('\r') && c1 == QLatin1Char('\n')))
      return offset + 1;
  }

  for (qsizetype half = kGraphemeWindow;; half *= 2) {
    qsizetype from = qMax<qsizetype>(0, offset - half);
    qsizetype to = qMin(len, offset + half);
    from = m_rope.snapToCodePoint(from, Rope::Snap::Backward); // keep surrogate pairs whole
    to = m_rope.snapToCodePoint(to, Rope::Snap::Forward);
    const QString window = m_rope.toString(from, to);
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, window);
    finder.setPosition(int(offset - from));
    const int next = finder.toNextBoundary();
    const qsizetype found = next < 0 ? to : from + next;
    const bool startTrusted = from == 0 || offset - from >= kGraphemeMargin;
    const bool endTrusted = to == len || to - found >= kGraphemeMargin;
    if ((startTrusted && endTrusted) || (from == 0 && to == len))
      return found;
  }
}

qsizetype TextBoundaries::previousGrapheme(qsizetype offset) const {
  const qsizetype len = m_rope.length();
  offset = qBound<qsizetype>(0, offset, len);
  if (offset <= 0)
    return 0;

  const QChar c0 = m_rope.at(offset - 1);
  if (c0.unicode() < 0x80) {
    if (offset == 1)
      return 0;
    const QChar prev = m_rope.at(offset - 2);
    if (prev.unicode() < 0x80 && !(prev == QLatin1Char('\r') && c0 == QLatin1Char('\n')))
      return offset - 1;
  }

  for (qsizetype half = kGraphemeWindow;; half *= 2) {
    qsizetype from = qMax<qsizetype>(0, offset - half);
    qsizetype to = qMin(len, offset + half);
    from = m_rope.snapToCodePoint(from, Rope::Snap::Backward);
    to = m_rope.snapToCodePoint(to, Rope::Snap::Forward);
    const QString window = m_rope.toString(from, to);
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, window);
    finder.setPosition(int(offset - from));
    const int prev = finder.toPreviousBoundary();
    const qsizetype found = prev < 0 ? from : from + prev;
    const bool startTrusted = from == 0 || found - from >= kGraphemeMargin;
    const bool endTrusted = to == len || to - offset >= kGraphemeMargin;
    if ((startTrusted && endTrusted) || (from == 0 && to == len))
      return found;
  }
}

qsizetype TextBoundaries::nextWordStart(qsizetype offset, bool big) const {
  Reader r(m_rope);
  qsizetype i = qBound<qsizetype>(0, offset, r.length());
  if (i >= r.length())
    return r.length();
  const Cls start = classAfter(r, i, big);
  if (start != Cls::Space) {
    while (i < r.length() && classAfter(r, i, big) == start)
      i = stepForward(r, i);
  }
  while (i < r.length() && classAfter(r, i, big) == Cls::Space)
    i = stepForward(r, i);
  return i;
}

qsizetype TextBoundaries::previousWordStart(qsizetype offset, bool big) const {
  Reader r(m_rope);
  qsizetype i = qBound<qsizetype>(0, offset, r.length());
  while (i > 0 && classBefore(r, i, big) == Cls::Space)
    i = stepBack(r, i);
  if (i == 0)
    return 0;
  const Cls cls = classBefore(r, i, big);
  while (i > 0 && classBefore(r, i, big) == cls)
    i = stepBack(r, i);
  return i;
}

qsizetype TextBoundaries::nextWordEnd(qsizetype offset, bool big) const {
  Reader r(m_rope);
  qsizetype i = qBound<qsizetype>(0, offset, r.length());
  while (i < r.length() && classAfter(r, i, big) == Cls::Space)
    i = stepForward(r, i);
  if (i >= r.length())
    return r.length();
  const Cls cls = classAfter(r, i, big);
  while (i < r.length() && classAfter(r, i, big) == cls)
    i = stepForward(r, i);
  return i;
}

qsizetype TextBoundaries::previousWordEnd(qsizetype offset, bool big) const {
  Reader r(m_rope);
  qsizetype i = qBound<qsizetype>(0, offset, r.length());
  if (i > 0 && classBefore(r, i, big) != Cls::Space) {
    // inside or at the end of a word: first leave its start, so we find an end strictly before
    const Cls cls = classBefore(r, i, big);
    while (i > 0 && classBefore(r, i, big) == cls)
      i = stepBack(r, i);
  }
  while (i > 0 && classBefore(r, i, big) == Cls::Space)
    i = stepBack(r, i);
  return i;
}

QPair<qsizetype, qsizetype> TextBoundaries::wordRangeAt(qsizetype offset, bool big) const {
  Reader r(m_rope);
  const qsizetype len = r.length();
  if (len == 0)
    return {0, 0};
  qsizetype pos = qBound<qsizetype>(0, offset, len);
  const Cls cls = pos < len ? classAfter(r, pos, big) : classBefore(r, pos, big);
  qsizetype a = pos, b = pos;
  if (pos == len) {
    a = stepBack(r, pos);
  } else {
    b = stepForward(r, pos);
  }
  while (a > 0 && classBefore(r, a, big) == cls)
    a = stepBack(r, a);
  while (b < len && classAfter(r, b, big) == cls)
    b = stepForward(r, b);
  return {a, b};
}

} // namespace qce
