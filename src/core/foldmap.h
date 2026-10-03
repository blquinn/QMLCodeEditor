#ifndef QCE_FOLDMAP_H
#define QCE_FOLDMAP_H

#include "core/anchorset.h"
#include "core/textchange.h"
#include "core/textdocument.h"

#include <QtCore/QList>
#include <QtCore/QtGlobal>

#include <optional>
#include <vector>

namespace qce {

// A fold hides the lines after its header: [startLine + 1, endLine]. The header stays visible.
struct FoldRange {
  qsizetype startLine = 0;
  qsizetype endLine = 0; // last hidden line; > startLine
  friend constexpr bool operator==(const FoldRange &, const FoldRange &) = default;
};

// A stretch of lines [first, last]; empty when last < first.
struct LineRange {
  qsizetype first = 0;
  qsizetype last = -1;
  bool isEmpty() const { return last < first; }
  void unite(qsizetype f, qsizetype l) {
    if (l < f)
      return;
    if (isEmpty())
      *this = {f, l};
    else
      *this = {qMin(first, f), qMax(last, l)};
  }
};

// First layer of the display map (ADR 0004, ADR 0014): buffer lines to the lines that stay visible
// after folding ("fold lines"). Each fold is a pair of anchors (ADR 0006), so folds follow edits; the
// hidden lines are kept as merged intervals with running totals, so conversions in both directions
// are O(log folds). An edit costs O(folds) integer work only when it adds or removes line breaks.
//
// A fold is dropped when edits leave it with nothing to hide. Folds may nest; two folds never share a
// header line.
class FoldMap {
public:
  explicit FoldMap(TextDocument *document);
  ~FoldMap();
  FoldMap(const FoldMap &) = delete;
  FoldMap &operator=(const FoldMap &) = delete;

  qsizetype bufferLineCount() const { return m_document->rope().lineCount(); }
  // Lines that are not hidden.
  qsizetype lineCount() const { return bufferLineCount() - hiddenTotal(); }
  qsizetype hiddenLineCount() const { return hiddenTotal(); }
  bool hasFolds() const { return !m_folds.empty(); }
  qsizetype foldCount() const { return qsizetype(m_folds.size()); }

  // A hidden line maps to the fold line of the visible line above it. Out-of-range arguments clamp.
  qsizetype foldLineForBufferLine(qsizetype bufferLine) const;
  qsizetype bufferLineForFoldLine(qsizetype foldLine) const;
  bool isHidden(qsizetype bufferLine) const;
  bool isVisible(qsizetype bufferLine) const { return !isHidden(bufferLine); }
  // The visible line a hidden line is folded into; a visible line is its own header.
  qsizetype visibleHeaderOf(qsizetype bufferLine) const;
  // The first visible line after `bufferLine`, or bufferLineCount() when there is none.
  qsizetype nextVisibleLine(qsizetype bufferLine) const;
  // Visible lines in [first, last]. The bounds may lie past the end of the text (an edit that removed
  // lines asks about the lines it replaced).
  qsizetype visibleLinesIn(qsizetype first, qsizetype last) const;

  bool isFolded(qsizetype header) const { return foldAtHeader(header).has_value(); }
  std::optional<FoldRange> foldAtHeader(qsizetype header) const;
  // All folds, ordered by header.
  QList<FoldRange> folds() const;
  // Folds whose header is in [first, last].
  QList<FoldRange> foldsWithHeaderIn(qsizetype first, qsizetype last) const;

  // Mutations return the lines whose visibility may have changed.
  LineRange fold(qsizetype header, qsizetype lastLine);
  LineRange unfold(qsizetype header);
  // Removes every fold that hides `bufferLine`.
  LineRange unfoldContaining(qsizetype bufferLine);
  LineRange unfoldAll();
  // Replaces all folds. Ranges outside the text or empty are ignored.
  LineRange setFolds(const QList<FoldRange> &ranges);

  // Brings the folds up to date with an edit already applied to the document's anchors. Returns the
  // lines (in the new text) whose visibility may have changed, apart from the lines the edit itself
  // replaced.
  LineRange applyChange(const TextChange &change);

  // Hidden stretches of lines, merged and ascending.
  struct Hidden {
    qsizetype first = 0;
    qsizetype last = 0;
    qsizetype before = 0; // hidden lines in earlier stretches
    qsizetype count() const { return last - first + 1; }
  };
  const std::vector<Hidden> &hiddenRanges() const { return m_hidden; }

private:
  struct Fold {
    AnchorId start = InvalidAnchor; // end of the header line
    AnchorId end = InvalidAnchor;   // end of the last hidden line
    qsizetype header = 0;
    qsizetype last = 0;
  };

  qsizetype hiddenTotal() const { return m_hidden.empty() ? 0 : m_hidden.back().before + m_hidden.back().count(); }
  qsizetype unclampedFoldLine(qsizetype bufferLine) const;
  void rebuildHidden();
  void dropAnchors(const Fold &fold);
  Fold makeFold(qsizetype header, qsizetype lastLine);
  std::vector<Fold>::const_iterator findHeader(qsizetype header) const;
  // Index of the last hidden stretch starting at or before `line`, or -1.
  qsizetype stretchAtOrBefore(qsizetype line) const;

  TextDocument *m_document;
  std::vector<Fold> m_folds; // by header
  std::vector<Hidden> m_hidden;
};

} // namespace qce

#endif // QCE_FOLDMAP_H
