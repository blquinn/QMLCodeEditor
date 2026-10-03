#ifndef QCE_DISPLAYMAP_H
#define QCE_DISPLAYMAP_H

#include "core/foldmap.h"
#include "core/textchange.h"
#include "core/textdocument.h"
#include "core/wrapbreaks.h"
#include "core/wrapmap.h"

#include <QtCore/QFutureWatcher>
#include <QtCore/QObject>

#include <unordered_map>

namespace qce {

// What one display row shows: a column range of one buffer line.
struct DisplayRow {
  qsizetype line = 0;
  qsizetype startColumn = 0;
  qsizetype endColumn = 0; // exclusive; line content only, never the line break
  qsizetype rowInLine = 0;
  qsizetype rowsInLine = 1;
  qreal indent = 0; // hanging indent of a continuation row, in pixels

  bool isFirst() const { return rowInLine == 0; }
  bool isLast() const { return rowInLine + 1 >= rowsInLine; }
  // The last column a cursor can rest on here. A cursor at the end of a soft-wrapped row would be
  // the start of the next one, so it stops one unit short.
  qsizetype lastCursorColumn() const { return isLast() ? endColumn : endColumn - 1; }
};

// The only authority on how buffer text maps to on-screen rows (ADR 0004, ADR 0011). Rendering,
// scrolling, hit-testing and cursor movement ask it and never assume one buffer line is one row.
//
// Layers: buffer lines -> FoldMap -> WrapMap -> display rows. With wrap off a row is a visible line.
// The WrapMap is indexed by buffer line; folded-away lines are flagged hidden in it and occupy no
// rows. With wrap on, a line's row count lives in a WrapMap and is exact once the line
// has been wrapped; until then it is an estimate. Queries wrap the lines they touch on demand, so
// the answer for the row or position asked about is always exact, while rows far away may shift as
// their lines get wrapped (see rowsReestimated).
class DisplayMap : public QObject {
  Q_OBJECT
public:
  // Lines longer than this are wrapped a few rows at a time instead of whole.
  static constexpr qsizetype kHugeLine = 1 << 16;

  explicit DisplayMap(TextDocument *document, QObject *parent = nullptr);

  const TextDocument *document() const { return m_document; }

  // Folding (ADR 0014). The fold layer decides which lines have rows; mutations here keep the wrap
  // layer in step and report through foldsChanged().
  const FoldMap &folds() const { return m_fold; }
  bool fold(qsizetype header, qsizetype lastLine);
  bool unfold(qsizetype header);
  // Unfolds every fold hiding `line` (the way cursors and edits reveal text).
  bool unfoldContaining(qsizetype line);
  bool unfoldAll();
  // Replaces all folds at once.
  void setFolds(const QList<FoldRange> &ranges);

  // Turns wrapping on, off or changes its width. Every line becomes an estimate again.
  void setWrapConfig(const WrapConfig &config);
  const WrapConfig &wrapConfig() const { return m_config; }
  bool wrapEnabled() const { return m_config.enabled(); }
  // Lines whose row count is still an estimate.
  qsizetype estimatedLineCount() const { return wrapEnabled() ? m_wrap.estimatedLineCount() : 0; }
  // Estimates are refined by a worker thread, a chunk of lines at a time, whenever the event loop
  // runs. Turn that off to wrap only what queries ask for (tests).
  void setBackgroundWrapping(bool enabled);

  qsizetype rowCount() const;
  // Buffer line shown on `row` (clamped to the valid rows).
  qsizetype lineForRow(qsizetype row) const;
  DisplayRow rowAt(qsizetype row) const;
  // First row of `line` and how many rows it occupies (a folded-away line has none).
  qsizetype firstRowOfLine(qsizetype line) const;
  qsizetype rowCountOfLine(qsizetype line) const;
  // `position`, or the end of the visible line it is folded into when it is in hidden text.
  TextPosition visiblePosition(TextPosition position) const;
  // The row showing buffer `position`. A position at a soft break belongs to the row after it.
  qsizetype rowForPosition(TextPosition position) const;

signals:
  // Rows [firstRow, firstRow + oldCount) were replaced by newCount rows; rows after shift.
  void rowsChanged(qsizetype firstRow, qsizetype oldCount, qsizetype newCount);
  // Wrapping a line showed it takes newCount rows, not the oldCount that was assumed. Nothing in the
  // text changed; row numbers after firstRow moved.
  void rowsReestimated(qsizetype firstRow, qsizetype oldCount, qsizetype newCount);
  // Everything is new (text reset or wrap settings changed); rebuild whatever you derived from row
  // numbers.
  void reset();
  // Lines were folded or unfolded; rows after the first changed one moved. Nothing in the text changed.
  void foldsChanged();
  // Background wrapping finished a chunk; this many lines are still estimates.
  void wrapProgress(qsizetype estimatedLines);

private:
  // Where the rows of one line begin. `starts` holds the start column of rows 1, 2, ...; it is the
  // whole story once `complete`, otherwise the line (a huge one) has only been scanned up to the
  // last start.
  struct LineBreaks {
    QList<qsizetype> starts;
    bool complete = false;
    qreal indent = 0;
  };

  void onChanged(const TextChange &change);
  void resetWrap();
  // Makes the WrapMap's hidden flags agree with the fold layer for lines [range.first, range.last].
  void syncHidden(LineRange range) const;
  void foldsDidChange(LineRange changed);
  qsizetype lineLength(qsizetype line) const;
  qsizetype estimateRows(qsizetype units, qreal indent) const;
  LineBreaks &breaksFor(qsizetype line) const;
  // Scans more of an incomplete line: until `rows` rows are determined (and then some).
  void extend(qsizetype line, LineBreaks &lb, qsizetype rows) const;
  void storeRows(qsizetype line, const LineBreaks &lb) const;
  // Wraps `line` so its row count is exact; huge lines are only wrapped as far as `column`.
  void resolve(qsizetype line, qsizetype column = 0) const;
  qsizetype locate(qsizetype row, qsizetype *rowInLine) const;
  DisplayRow makeRow(qsizetype line, qsizetype rowInLine) const;
  void shiftBreaks(qsizetype first, qsizetype oldCount, qsizetype newCount);

  // What a worker found out about a stretch of lines.
  struct ChunkResult {
    quint64 generation = 0;
    quint64 version = 0;
    qsizetype firstLine = 0;
    QList<quint32> rows;         // exact row counts of lines [firstLine, firstLine + rows.size())
    QList<qsizetype> hugeStarts; // for a single huge line: all the starts found so far
    qsizetype hugeKnown = 0;     // how many of them were known before this chunk
    bool hugeComplete = false;
    bool huge = false;
  };

  void pumpBackground();
  void applyChunk(const ChunkResult &result);

  TextDocument *m_document;
  FoldMap m_fold;
  bool m_background = true;
  quint64 m_generation = 1; // bumped when the config changes, so stale chunks are dropped
  qsizetype m_cursor = 0;   // where the background looks for the next estimate
  QFutureWatcher<ChunkResult> m_watcher;
  bool m_running = false;
  WrapConfig m_config;
  // Wrapping state is a cache of what the text and config imply, filled when queries ask.
  mutable WrapMap m_wrap;
  mutable std::unordered_map<qsizetype, LineBreaks> m_breaks;
};

} // namespace qce

#endif // QCE_DISPLAYMAP_H
