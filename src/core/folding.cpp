#include "core/folding.h"

namespace qce::folding {

bool toggle(DisplayMap &map, FoldProvider &provider, const TextSnapshot &text, qsizetype line) {
  if (map.folds().isFolded(line))
    return map.unfold(line);
  if (const auto range = provider.rangeAt(text, line))
    return map.fold(range->startLine, range->endLine);
  return false;
}

bool foldAt(DisplayMap &map, FoldProvider &provider, const TextSnapshot &text, qsizetype line) {
  if (const auto range = provider.rangeEnclosing(text, line, &map.folds()))
    return map.fold(range->startLine, range->endLine);
  return false;
}

bool unfoldAt(DisplayMap &map, qsizetype line) {
  if (map.folds().isFolded(line))
    return map.unfold(line);
  return map.unfoldContaining(line);
}

bool foldAll(DisplayMap &map, FoldProvider &provider, const TextSnapshot &text) {
  const QList<FoldRange> ranges = provider.foldRanges(text, 0, text.lineCount() - 1);
  if (ranges.isEmpty() || ranges == map.folds().folds())
    return false;
  map.setFolds(ranges);
  return true;
}

bool unfoldAll(DisplayMap &map) { return map.unfoldAll(); }

bool foldToLevel(DisplayMap &map, FoldProvider &provider, const TextSnapshot &text, int level) {
  const QList<FoldRange> ranges = provider.foldRanges(text, 0, text.lineCount() - 1);
  const QList<int> depths = foldDepths(ranges);
  QList<FoldRange> chosen;
  for (qsizetype i = 0; i < ranges.size(); ++i)
    if (depths[i] == level)
      chosen.append(ranges[i]);
  if (chosen == map.folds().folds())
    return false;
  map.setFolds(chosen);
  return true;
}

} // namespace qce::folding
