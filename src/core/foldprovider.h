#ifndef QCE_FOLDPROVIDER_H
#define QCE_FOLDPROVIDER_H

#include "core/foldmap.h"
#include "core/textsnapshot.h"

#include <QtCore/QList>
#include <QtCore/QObject>

#include <optional>
#include <unordered_map>

namespace qce {

// Says which stretches of text can be folded (ADR 0014). The editor asks for the lines it is about to
// draw, so implementations answer for a few hundred lines at a time; asking for the whole text (fold
// all) is allowed to be slower.
class FoldProvider : public QObject {
  Q_OBJECT
public:
  static constexpr qsizetype AllLines = -1;

  explicit FoldProvider(QObject *parent = nullptr) : QObject(parent) {}

  // Ranges whose header line is in [firstLine, lastLine], ordered by header. A range hides
  // [startLine + 1, endLine], so endLine > startLine. At most one range per header.
  virtual QList<FoldRange> foldRanges(const TextSnapshot &text, qsizetype firstLine, qsizetype lastLine) = 0;

  // The range that starts on `line`, if any.
  std::optional<FoldRange> rangeAt(const TextSnapshot &text, qsizetype line);
  // The innermost range with startLine <= line <= endLine that is not already folded in `folded`
  // (pass nullptr to ignore folds). Looks at most `maxLinesUp` lines above `line`.
  std::optional<FoldRange>
  rangeEnclosing(const TextSnapshot &text, qsizetype line, const FoldMap *folded = nullptr, qsizetype maxLinesUp = 5000);

signals:
  // Ranges starting in these lines may have changed (all lines when firstLine == AllLines).
  void invalidated(qsizetype firstLine, qsizetype lastLine);
};

// Folds by indentation: a non-blank line followed by deeper lines starts a range that runs to the last
// non-blank line before indentation falls back to the header's level. The fallback for text without a
// grammar.
class IndentFoldProvider : public FoldProvider {
  Q_OBJECT
public:
  explicit IndentFoldProvider(QObject *parent = nullptr) : FoldProvider(parent) {}

  int tabWidth() const { return m_tabWidth; }
  void setTabWidth(int columns);
  // How many lines past the requested ones are read to find where a range ends; a range longer than
  // that is not offered (fold all ignores the limit).
  qsizetype maxScanLines() const { return m_maxScan; }
  void setMaxScanLines(qsizetype lines);

  QList<FoldRange> foldRanges(const TextSnapshot &text, qsizetype firstLine, qsizetype lastLine) override;

private:
  static constexpr qsizetype kBlockLines = 256;
  QList<FoldRange> computeBlock(const TextSnapshot &text, qsizetype firstLine, qsizetype lastLine, qsizetype scanLimit) const;

  int m_tabWidth = 4;
  qsizetype m_maxScan = 20000;
  // Ranges by block of kBlockLines headers, valid for one document version.
  quint64 m_cacheVersion = ~quint64(0);
  std::unordered_map<qsizetype, QList<FoldRange>> m_blocks;
};

// Where a range whose node ends at (endRow, endColumn) should stop hiding. A node ending at column 0
// ends on the row before. The last row stays visible when it only holds a closing token (`}`, `)`,
// `</div>`, a code fence, `*/`), or always with `keepLastLine`.
qsizetype foldEndLine(const Rope &rope, qsizetype endRow, qsizetype endColumn, bool keepLastLine = false);

// Nesting level of each range, 1 for outermost. `ranges` must be ordered by header.
QList<int> foldDepths(const QList<FoldRange> &ranges);

} // namespace qce

#endif // QCE_FOLDPROVIDER_H
