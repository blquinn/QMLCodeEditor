#include "core/indentation.h"

namespace qce {

std::optional<IndentationGuess> detectIndentation(const Rope &rope, qsizetype maxLines) {
  qsizetype tabLines = 0, spaceLines = 0;
  qsizetype histogram[9] = {};
  qsizetype previous = 0; // spaces leading the previous non-blank space-indented (or unindented) line

  // Per-line state, carried across chunks.
  qsizetype lines = 0, spaces = 0;
  bool leading = true, tabLed = false, started = false;

  auto endLine = [&](bool blank) {
    ++lines;
    if (!blank && started) {
      if (tabLed) {
        ++tabLines;
      } else {
        ++spaceLines;
        const qsizetype delta = qAbs(spaces - previous);
        if (delta >= 2 && delta <= 8)
          ++histogram[delta];
      }
    }
    if (!blank) {
      previous = tabLed ? 0 : spaces;
    }
    spaces = 0;
    leading = true;
    tabLed = started = false;
  };

  ChunkIterator it(rope);
  QStringView chunk;
  while (lines < maxLines && it.next(&chunk)) {
    for (QChar ch : chunk) {
      const char16_t c = ch.unicode();
      if (c == u'\n') {
        endLine(leading); // only whitespace seen: blank
        if (lines >= maxLines)
          break;
      } else if (leading && c == u' ') {
        ++spaces;
        started = true;
      } else if (leading && c == u'\t') {
        if (!started)
          tabLed = true;
        started = true;
      } else if (leading && c == u'\r') {
        // CRLF: ignore
      } else if (leading) {
        leading = false;
      }
    }
  }

  if (tabLines == 0 && spaceLines == 0)
    return std::nullopt;
  IndentationGuess guess;
  guess.insertSpaces = spaceLines >= tabLines;
  if (guess.insertSpaces) {
    // Ties go 4 > 2 > 8 > 3.
    qsizetype best = 0;
    for (int w : {4, 2, 8, 3}) {
      if (histogram[w] > best) {
        best = histogram[w];
        guess.indentWidth = w;
      }
    }
  }
  return guess;
}

} // namespace qce
