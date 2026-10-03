#include "core/displaymap.h"

#include <QtCore/QRandomGenerator>
#include <QtTest>

#include <set>

using namespace qce;
using namespace Qt::StringLiterals;

namespace {

QString numberedLines(int count, const QString &suffix = {}) {
  QStringList lines;
  for (int i = 0; i < count; ++i)
    lines << u"line%1"_s.arg(i) + suffix;
  return lines.join(u'\n');
}

// Lines hidden by `folds`, the slow way.
std::set<qsizetype> hiddenBy(const QList<FoldRange> &folds) {
  std::set<qsizetype> hidden;
  for (const FoldRange &f : folds)
    for (qsizetype l = f.startLine + 1; l <= f.endLine; ++l)
      hidden.insert(l);
  return hidden;
}

WrapConfig gridConfig(int column) {
  WrapConfig c;
  c.mode = WrapMode::Column;
  c.column = column;
  c.wordBreak = true;
  c.hangingIndent = false;
  c.measure = std::make_shared<GridWrapMeasure>(4);
  return c;
}

void resolveAll(const DisplayMap &map) {
  for (qsizetype l = 0; l < map.document()->rope().lineCount(); ++l)
    map.rowCountOfLine(l);
}

QList<DisplayRow> allRows(const DisplayMap &map) {
  resolveAll(map);
  QList<DisplayRow> rows;
  for (qsizetype r = 0; r < map.rowCount(); ++r)
    rows.append(map.rowAt(r));
  return rows;
}

void verifyAgainstBruteForce(const FoldMap &fold) {
  const QList<FoldRange> ranges = fold.folds();
  const std::set<qsizetype> hidden = hiddenBy(ranges);
  const qsizetype lines = fold.bufferLineCount();
  QCOMPARE(fold.hiddenLineCount(), qsizetype(hidden.size()));
  QCOMPARE(fold.lineCount(), lines - qsizetype(hidden.size()));
  QList<qsizetype> visible;
  for (qsizetype l = 0; l < lines; ++l)
    if (!hidden.count(l))
      visible.append(l);
  QCOMPARE(visible.size(), fold.lineCount());
  qsizetype header = 0, nextVisible = 0;
  for (qsizetype l = 0; l < lines; ++l) {
    QCOMPARE(fold.isHidden(l), hidden.count(l) == 1);
    if (!hidden.count(l)) {
      header = l;
      QCOMPARE(fold.visibleHeaderOf(l), l);
    } else {
      QCOMPARE(fold.visibleHeaderOf(l), header);
    }
    QCOMPARE(fold.foldLineForBufferLine(l), qsizetype(std::lower_bound(visible.begin(), visible.end(), header) - visible.begin()));
    nextVisible = l + 1;
    while (nextVisible < lines && hidden.count(nextVisible))
      ++nextVisible;
    QCOMPARE(fold.nextVisibleLine(l), nextVisible);
  }
  for (qsizetype f = 0; f < visible.size(); ++f)
    QCOMPARE(fold.bufferLineForFoldLine(f), visible[f]);
  for (qsizetype a = 0; a < lines; a += 1 + a % 3)
    for (qsizetype b = a; b < lines; b += 1 + b % 5) {
      qsizetype n = 0;
      for (qsizetype l = a; l <= b; ++l)
        n += hidden.count(l) ? 0 : 1;
      QCOMPARE(fold.visibleLinesIn(a, b), n);
    }
}

} // namespace

class TstFoldMap : public QObject {
  Q_OBJECT
private slots:
  void startsEmpty() {
    TextDocument doc;
    doc.setText(u"a\nb\nc"_s);
    FoldMap fold(&doc);
    QVERIFY(!fold.hasFolds());
    QCOMPARE(fold.lineCount(), 3);
    QCOMPARE(fold.foldLineForBufferLine(2), 2);
    QCOMPARE(fold.bufferLineForFoldLine(9), 2);
    QVERIFY(fold.isVisible(1));
  }

  void foldHidesTheLinesAfterTheHeader() {
    TextDocument doc;
    doc.setText(numberedLines(10));
    FoldMap fold(&doc);
    const LineRange changed = fold.fold(2, 5);
    QCOMPARE(changed.first, 3);
    QCOMPARE(changed.last, 5);
    QCOMPARE(fold.lineCount(), 7);
    QVERIFY(fold.isHidden(3));
    QVERIFY(fold.isHidden(5));
    QVERIFY(!fold.isHidden(2));
    QVERIFY(!fold.isHidden(6));
    QCOMPARE(fold.foldLineForBufferLine(4), 2); // a hidden line is shown by its header
    QCOMPARE(fold.foldLineForBufferLine(6), 3);
    QCOMPARE(fold.bufferLineForFoldLine(3), 6);
    QCOMPARE(fold.nextVisibleLine(2), 6);
    QCOMPARE(fold.visibleHeaderOf(4), 2);
    QVERIFY(fold.isFolded(2));
    QVERIFY(!fold.isFolded(3));
    QVERIFY(fold.fold(2, 5).isEmpty()); // unchanged
    QVERIFY(fold.fold(0, 0).isEmpty()); // nothing to hide
    QVERIFY(fold.fold(8, 20).isEmpty()); // past the end
    QVERIFY(!fold.unfold(2).isEmpty());
    QCOMPARE(fold.lineCount(), 10);
  }

  void nestedAndAdjacentFoldsMerge() {
    TextDocument doc;
    doc.setText(numberedLines(20));
    FoldMap fold(&doc);
    fold.setFolds({{1, 10}, {3, 5}, {10, 12}, {15, 17}});
    verifyAgainstBruteForce(fold);
    QCOMPARE(fold.hiddenRanges().size(), 2u); // [2,12] and [16,17]
    // Unfolding the outer one reveals the inner folds still folded.
    fold.unfold(1);
    verifyAgainstBruteForce(fold);
    QVERIFY(fold.isHidden(4));
    QVERIFY(!fold.isHidden(2));
    QVERIFY(!fold.unfoldContaining(4).isEmpty());
    QVERIFY(!fold.isHidden(4));
    QVERIFY(fold.unfoldContaining(4).isEmpty());
  }

  void conversionsMatchBruteForce() {
    QRandomGenerator rng(3);
    for (int round = 0; round < 40; ++round) {
      TextDocument doc;
      const int lines = 1 + rng.bounded(60);
      doc.setText(numberedLines(lines));
      FoldMap fold(&doc);
      QList<FoldRange> ranges;
      for (int i = rng.bounded(12); i > 0; --i) {
        const qsizetype a = rng.bounded(lines);
        ranges.append({a, a + 1 + rng.bounded(15)});
      }
      fold.setFolds(ranges);
      verifyAgainstBruteForce(fold);
    }
  }

  void typingOnTheHeaderKeepsTheFold() {
    TextDocument doc;
    doc.setText(u"head {\nbody1\nbody2\n}\nafter"_s);
    DisplayMap map(&doc);
    const FoldMap &fold = map.folds();
    map.fold(0, 2);
    doc.insert(6, u" // note"_s); // at the end of the header
    QVERIFY(fold.isFolded(0));
    QCOMPARE(fold.foldAtHeader(0)->endLine, 2);
    doc.insert(0, u"x"_s);
    QVERIFY(fold.isFolded(0));
  }

  void foldsFollowLineBreaks() {
    TextDocument doc;
    doc.setText(numberedLines(8));
    DisplayMap map(&doc);
    const FoldMap &fold = map.folds();
    map.fold(3, 5);
    doc.insert(0, u"new\n"_s); // above
    QCOMPARE(fold.foldAtHeader(4)->endLine, 6);
    doc.insert(doc.length(), u"\nmore\nlines"_s); // below
    QCOMPARE(fold.foldAtHeader(4)->endLine, 6);
    // Lines added inside the hidden body grow the fold.
    const qsizetype inside = doc.rope().lineStart(6);
    doc.insert(inside, u"extra\n"_s);
    QCOMPARE(fold.foldAtHeader(4)->endLine, 7);
    // Removing lines above moves it back.
    doc.remove(0, 4);
    QCOMPARE(fold.foldAtHeader(3)->endLine, 6);
    verifyAgainstBruteForce(fold);
  }

  void deletingTheBodyDropsTheFold() {
    TextDocument doc;
    doc.setText(u"head {\nbody1\nbody2\n}\nafter"_s);
    DisplayMap map(&doc);
    const FoldMap &fold = map.folds();
    map.fold(0, 2);
    doc.remove(doc.rope().lineEnd(0), doc.rope().lineEnd(2)); // the whole body, with its line breaks
    QVERIFY(!fold.hasFolds());
    QCOMPARE(fold.lineCount(), doc.rope().lineCount());
  }

  void joiningTheHeaderWithTheBodyKeepsTheRest() {
    TextDocument doc;
    doc.setText(u"head {\nbody1\nbody2\nbody3\n}"_s);
    DisplayMap map(&doc);
    const FoldMap &fold = map.folds();
    map.fold(0, 3);
    doc.remove(doc.rope().lineEnd(0), doc.rope().lineStart(1)); // Delete at the end of the header
    QCOMPARE(fold.foldAtHeader(0)->endLine, 2);
    verifyAgainstBruteForce(fold);
  }

  void resettingTheTextRemovesFolds() {
    TextDocument doc;
    doc.setText(numberedLines(6));
    DisplayMap map(&doc);
    map.fold(1, 3);
    QVERIFY(map.folds().hasFolds());
    doc.setText(numberedLines(6));
    QVERIFY(!map.folds().hasFolds());
    QCOMPARE(map.rowCount(), 6);
  }

  void randomEditsKeepFoldsConsistent() {
    QRandomGenerator rng(11);
    for (int round = 0; round < 6; ++round) {
      TextDocument doc;
      doc.setText(numberedLines(40, u" x"_s));
      DisplayMap map(&doc);
      QSignalSpy folded(&map, &DisplayMap::foldsChanged);
      for (int i = 0; i < 10; ++i) {
        const qsizetype a = rng.bounded(35);
        map.fold(a, a + 1 + rng.bounded(6));
      }
      for (int step = 0; step < 200; ++step) {
        const qsizetype length = doc.length();
        const qsizetype a = rng.bounded(length + 1);
        const qsizetype b = qMin<qsizetype>(length, a + rng.bounded(rng.bounded(5) == 0 ? 80 : 4));
        QString text;
        for (int i = rng.bounded(4); i > 0; --i)
          text += rng.bounded(3) == 0 ? u"\n"_s : u"ab"_s;
        doc.replace(a, b, text);
        if (step % 20 == 0)
          map.fold(rng.bounded(doc.rope().lineCount()), doc.rope().lineCount() - 1);
        const QList<FoldRange> ranges = map.folds().folds();
        qsizetype previous = -1;
        for (const FoldRange &f : ranges) {
          QVERIFY(f.startLine > previous); // sorted, one per header
          QVERIFY(f.endLine > f.startLine);
          QVERIFY(f.endLine < doc.rope().lineCount());
          previous = f.startLine;
        }
        verifyAgainstBruteForce(map.folds());
        QCOMPARE(map.rowCount(), map.folds().lineCount());
      }
    }
  }

  // ---- With wrap ---------------------------------------------------------------------------

  void wrappedHeaderKeepsItsRows() {
    TextDocument doc;
    doc.setText(u"aaaa bbbb cccc dddd\nx\ny\nzzzz zzzz zzzz\nlast"_s);
    DisplayMap map(&doc);
    map.setBackgroundWrapping(false);
    map.setWrapConfig(gridConfig(6));
    resolveAll(map);
    const qsizetype before = map.rowCount();
    const qsizetype headerRows = map.rowCountOfLine(0);
    QVERIFY(headerRows > 1);
    QSignalSpy spy(&map, &DisplayMap::foldsChanged);
    QVERIFY(map.fold(0, 2));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(map.rowCount(), before - 2);
    QCOMPARE(map.rowCountOfLine(0), headerRows);
    QCOMPARE(map.rowCountOfLine(1), 0);
    QCOMPARE(map.firstRowOfLine(1), headerRows);
    QCOMPARE(map.firstRowOfLine(3), headerRows);
    QCOMPARE(map.lineForRow(headerRows), 3);
    // A position in hidden text belongs to the last row of the header.
    QCOMPARE(map.rowForPosition({1, 0}), headerRows - 1);
    QCOMPARE(map.rowForPosition({2, 1}), headerRows - 1);
    QVERIFY(map.unfold(0));
    QCOMPARE(map.rowCount(), before);
    QCOMPARE(map.rowForPosition({1, 0}), headerRows);
  }

  void noWrapRowQueriesSkipHiddenLines() {
    TextDocument doc;
    doc.setText(numberedLines(8));
    DisplayMap map(&doc);
    map.fold(1, 3);
    QCOMPARE(map.rowCount(), 6);
    QCOMPARE(map.lineForRow(1), 1);
    QCOMPARE(map.lineForRow(2), 4);
    QCOMPARE(map.firstRowOfLine(4), 2);
    QCOMPARE(map.firstRowOfLine(2), 2);
    QCOMPARE(map.rowCountOfLine(2), 0);
    QCOMPARE(map.rowForPosition({2, 3}), 1);
  }

  void wrapAndFoldsMatchRowsFromScratch() {
    QRandomGenerator rng(5);
    for (int round = 0; round < 5; ++round) {
      const WrapConfig config = gridConfig(5 + round * 3);
      TextDocument doc;
      QString text;
      for (int l = 0; l < 40; ++l) {
        if (l)
          text += u'\n';
        for (int w = rng.bounded(8); w > 0; --w)
          text += u"word "_s;
      }
      doc.setText(text);
      DisplayMap map(&doc);
      map.setBackgroundWrapping(false);
      map.setWrapConfig(config);
      for (int i = 0; i < 6; ++i) {
        const qsizetype a = rng.bounded(35);
        map.fold(a, a + 1 + rng.bounded(5));
      }
      for (int step = 0; step < 120; ++step) {
        const qsizetype length = doc.length();
        const qsizetype a = rng.bounded(length + 1);
        const qsizetype b = qMin<qsizetype>(length, a + rng.bounded(rng.bounded(5) == 0 ? 60 : 4));
        QString edit;
        for (int i = rng.bounded(4); i > 0; --i)
          edit += rng.bounded(3) == 0 ? u"\n"_s : u"ab "_s;
        doc.replace(a, b, edit);
        switch (step % 15) {
        case 3: map.unfold(map.lineForRow(rng.bounded(map.rowCount()))); break;
        case 6: map.fold(rng.bounded(doc.rope().lineCount()), doc.rope().lineCount() - 1); break;
        case 9: map.unfoldContaining(rng.bounded(doc.rope().lineCount())); break;
        default: break;
        }

        TextDocument fresh;
        fresh.setText(doc.rope().toString());
        DisplayMap expected(&fresh);
        expected.setBackgroundWrapping(false);
        expected.setWrapConfig(config);
        expected.setFolds(map.folds().folds());
        const QList<DisplayRow> want = allRows(expected);
        const QList<DisplayRow> got = allRows(map);
        QCOMPARE(got.size(), want.size());
        for (qsizetype r = 0; r < got.size(); ++r) {
          QVERIFY2(
            got[r].line == want[r].line && got[r].startColumn == want[r].startColumn &&
              got[r].endColumn == want[r].endColumn && got[r].rowsInLine == want[r].rowsInLine,
            qPrintable(u"round %1 step %2 row %3"_s.arg(round).arg(step).arg(r))
          );
        }
        for (qsizetype l = 0; l < doc.rope().lineCount(); ++l)
          QCOMPARE(map.firstRowOfLine(l), expected.firstRowOfLine(l));
      }
    }
  }

  void togglingWrapKeepsFolds() {
    TextDocument doc;
    doc.setText(numberedLines(10, u" aaaa bbbb cccc"_s));
    DisplayMap map(&doc);
    map.setBackgroundWrapping(false);
    map.fold(2, 6);
    map.setWrapConfig(gridConfig(8));
    resolveAll(map);
    QCOMPARE(map.rowCountOfLine(4), 0);
    QCOMPARE(map.lineForRow(map.firstRowOfLine(2) + map.rowCountOfLine(2)), 7);
    map.setWrapConfig({});
    QCOMPARE(map.rowCount(), 6);
  }

private:
  void verifyAgainstBruteForce(const FoldMap &fold) { ::verifyAgainstBruteForce(fold); }
};

QTEST_GUILESS_MAIN(TstFoldMap)
#include "tst_foldmap.moc"
