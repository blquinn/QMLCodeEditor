#include "core/displaymap.h"
#include "core/wrapbreaks.h"
#include "core/wrapmap.h"

#include <QtCore/QRandomGenerator>
#include <QtTest>

using namespace qce;
using namespace Qt::StringLiterals;
using Entry = WrapMap::Entry;

namespace {

WrapConfig gridConfig(int column, bool word = true, bool hanging = false, int extra = 0, int tabWidth = 4) {
  WrapConfig c;
  c.mode = WrapMode::Column;
  c.column = column;
  c.wordBreak = word;
  c.hangingIndent = hanging;
  c.extraIndent = extra;
  c.measure = std::make_shared<GridWrapMeasure>(tabWidth);
  return c;
}

// The starts of the rows of one line, wrapped in one go.
QList<qsizetype> wrapLine(const QString &text, const WrapConfig &config) {
  const Rope rope = Rope::fromString(text);
  QList<qsizetype> starts;
  const qreal indent = wrapIndent(config, rope, 0, text.size());
  wrapRows(rope, 0, text.size(), config, indent, 0, true, std::numeric_limits<qsizetype>::max(), starts);
  return starts;
}

QString randomText(QRandomGenerator &rng, int lines, int maxLen) {
  static const QList<QString> pieces = {u"a"_s, u"b"_s, u"c"_s, u"word"_s, u" "_s, u" "_s, u"  "_s, u"\t"_s,
                                        u"é"_s, u"é"_s, u"日"_s, u"😀"_s, u"x"_s, u"-"_s};
  QString text;
  for (int l = 0; l < lines; ++l) {
    if (l > 0)
      text += u'\n';
    const int len = rng.bounded(maxLen + 1);
    for (int i = 0; i < len; ++i)
      text += pieces[rng.bounded(pieces.size())];
  }
  return text;
}

QList<DisplayRow> allRows(const DisplayMap &map) {
  QList<DisplayRow> rows;
  for (qsizetype r = 0; r < map.rowCount(); ++r)
    rows.append(map.rowAt(r));
  return rows;
}

} // namespace

class TstWrapMap : public QObject {
  Q_OBJECT
private slots:
  // ---- WrapMap against a plain list ----------------------------------------------------------

  void emptyAndSimple() {
    WrapMap map;
    QCOMPARE(map.lineCount(), 0);
    QCOMPARE(map.rowCount(), 0);
    map.reset(3, 1, false);
    QCOMPARE(map.rowCount(), 3);
    map.setLine(1, {4, false});
    QCOMPARE(map.rowCount(), 6);
    QCOMPARE(map.firstRowOfLine(2), 5);
    qsizetype in = -1;
    QCOMPARE(map.lineAtRow(3, &in), 1);
    QCOMPARE(in, 2);
    QCOMPARE(map.lineAtRow(5, &in), 2);
    QCOMPARE(in, 0);
    QCOMPARE(map.lineAtRow(99, &in), 2); // clamped
  }

  void differentialAgainstList() {
    QRandomGenerator rng(7);
    WrapMap map;
    QList<Entry> model;
    auto randomEntry = [&] { return Entry{quint32(1 + rng.bounded(rng.bounded(10) == 0 ? 50 : 3)), rng.bounded(3) == 0}; };
    auto fill = [&](qsizetype n) {
      QList<Entry> out;
      for (qsizetype i = 0; i < n; ++i)
        out.append(randomEntry());
      return out;
    };
    model = fill(5000);
    map.reset(0, 1, false);
    map.splice(0, 0, model);

    auto verify = [&] {
      QCOMPARE(map.lineCount(), model.size());
      qsizetype rows = 0, estimated = 0;
      for (const Entry &e : std::as_const(model)) {
        rows += e.rows;
        estimated += e.estimated;
      }
      QCOMPARE(map.rowCount(), rows);
      QCOMPARE(map.estimatedLineCount(), estimated);
      for (int k = 0; k < 20 && !model.isEmpty(); ++k) {
        const qsizetype line = rng.bounded(model.size());
        const Entry e = map.entry(line);
        QCOMPARE(e.rows, model[line].rows);
        QCOMPARE(e.estimated, model[line].estimated);
        qsizetype before = 0;
        for (qsizetype i = 0; i < line; ++i)
          before += model[i].rows;
        QCOMPARE(map.firstRowOfLine(line), before);
        const qsizetype row = before + rng.bounded(model[line].rows);
        qsizetype in = -1;
        QCOMPARE(map.lineAtRow(row, &in), line);
        QCOMPARE(in, row - before);
      }
      QCOMPARE(map.firstRowOfLine(model.size()), rows);
      qsizetype from = model.isEmpty() ? 0 : rng.bounded(model.size());
      qsizetype expect = -1;
      for (qsizetype i = from; i < model.size(); ++i)
        if (model[i].estimated) {
          expect = i;
          break;
        }
      QCOMPARE(map.nextEstimated(from), expect);
    };
    verify();
    for (int step = 0; step < 300; ++step) {
      const int op = rng.bounded(4);
      if (op == 0 && !model.isEmpty()) { // same-size overwrite
        const qsizetype first = rng.bounded(model.size());
        const QList<Entry> e = fill(qMin<qsizetype>(rng.bounded(40), model.size() - first));
        map.setLines(first, e);
        for (qsizetype i = 0; i < e.size(); ++i)
          model[first + i] = e[i];
      } else {
        const qsizetype first = rng.bounded(model.size() + 1);
        const qsizetype maxOld = qMin<qsizetype>(model.size() - first, rng.bounded(8) == 0 ? 3000 : 30);
        const qsizetype old = rng.bounded(maxOld + 1);
        const QList<Entry> e = fill(rng.bounded(8) == 0 ? rng.bounded(3000) : rng.bounded(40));
        map.splice(first, old, e);
        model.remove(first, old);
        for (qsizetype i = 0; i < e.size(); ++i)
          model.insert(first + i, e[i]);
      }
      verify();
    }
  }

  // ---- Breaking a line ------------------------------------------------------------------------

  void breaksAtWords() {
    // 10 cells per row: "hello " | "world foo " | "bar"
    QCOMPARE(wrapLine(u"hello world foo bar"_s, gridConfig(10)), (QList<qsizetype>{6, 16}));
    QCOMPARE(wrapLine(u"short"_s, gridConfig(10)), QList<qsizetype>{});
    QCOMPARE(wrapLine(u""_s, gridConfig(10)), QList<qsizetype>{});
  }

  void wholeWordsLongerThanARowBreakInside() {
    QCOMPARE(wrapLine(u"aaaaaaaaaaaaaaaaaaaaaaaaa"_s, gridConfig(10)), (QList<qsizetype>{10, 20}));
    QCOMPARE(wrapLine(u"ab cdefghijklmnopqrstuvwxyz"_s, gridConfig(10)), (QList<qsizetype>{3, 13, 23}));
  }

  void characterWrapIgnoresWords() {
    QCOMPARE(wrapLine(u"hello world foo bar"_s, gridConfig(10, false)), (QList<qsizetype>{10}));
  }

  void trailingWhitespaceHangs() {
    // The spaces after "aaaa" run past the edge instead of forcing a break.
    QCOMPARE(wrapLine(u"aaaa          b"_s, gridConfig(10)), (QList<qsizetype>{14}));
  }

  void tabsAdvanceToStops() {
    // tab stops every 4 cells: "ab" + tab = 4 cells, "\t" = 4
    QCOMPARE(wrapLine(u"ab\t\tcd\tef"_s, gridConfig(8)), (QList<qsizetype>{4}));
  }

  void wideCharactersTakeTwoCells() {
    QCOMPARE(wrapLine(u"日本語日本語"_s, gridConfig(6)), (QList<qsizetype>{3}));
    // One wide character never leaves a row empty, even in a 1-cell world (rows are 4 cells at least).
    QCOMPARE(wrapLine(u"日日日"_s, gridConfig(1)), (QList<qsizetype>{2}));
  }

  void clustersAreNotSplit() {
    // 10 cells: 9 letters, then an e with a combining accent that must stay together.
    QCOMPARE(wrapLine(u"abcdefghiéj"_s, gridConfig(10)), (QList<qsizetype>{11}));
    // A surrogate pair is one character.
    const QString s = u"aaaaaaaaa😀b"_s; // 9 letters + 2-cell emoji overflows -> emoji moves to next row
    QCOMPARE(wrapLine(s, gridConfig(10)), (QList<qsizetype>{9}));
  }

  void hangingIndentNarrowsContinuationRows() {
    // 4 spaces of indent: continuation rows are 10 - 4 = 6 cells wide. With 2 extra the indent would
    // be 6, but it is capped at half a row (5).
    const QString s = u"    aaaaaaaaaaaaaaaaaaaa"_s;
    QCOMPARE(wrapLine(s, gridConfig(10, true, true)), (QList<qsizetype>{10, 16, 22}));
    QCOMPARE(wrapLine(s, gridConfig(10, true, false)), (QList<qsizetype>{10, 20}));
    QCOMPARE(wrapLine(s, gridConfig(10, true, true, 2)), (QList<qsizetype>{10, 15, 20}));
  }

  void hangingIndentIsCapped() {
    const WrapConfig c = gridConfig(10, true, true);
    const Rope rope = Rope::fromString(u"                      x"_s);
    QCOMPARE(wrapIndent(c, rope, 0, 23), 5.0); // half a row
  }

  void everyRowHoldsSomething() {
    QRandomGenerator rng(11);
    for (int i = 0; i < 200; ++i) {
      const QString text = randomText(rng, 1, 60);
      for (int column : {1, 4, 5, 9, 30}) {
        const QList<qsizetype> starts = wrapLine(text, gridConfig(column, i % 2, i % 3 == 0, i % 5));
        qsizetype previous = 0;
        for (qsizetype s : starts) {
          QVERIFY(s > previous);
          QVERIFY(s < text.size());
          // Never inside a surrogate pair.
          QVERIFY(!(text[s].isLowSurrogate() && text[s - 1].isHighSurrogate()));
          previous = s;
        }
      }
    }
  }

  void resumingScanGivesTheSameBreaks() {
    QRandomGenerator rng(5);
    const WrapConfig config = gridConfig(12);
    for (int i = 0; i < 50; ++i) {
      const QString text = randomText(rng, 1, 400);
      const Rope rope = Rope::fromString(text);
      const QList<qsizetype> whole = wrapLine(text, config);
      QList<qsizetype> parts;
      qsizetype at = 0;
      bool first = true;
      while (at < text.size()) {
        at = wrapRows(rope, 0, text.size(), config, 0, at, first, 3, parts);
        first = false;
      }
      QCOMPARE(parts, whole);
    }
  }

  // ---- The display map ------------------------------------------------------------------------

  void wrappedMapShowsRows() {
    TextDocument doc;
    doc.setText(u"hello world foo bar\nx\n\nabcdefghijklmnopqrstuvwxy"_s);
    DisplayMap map(&doc);
    map.setWrapConfig(gridConfig(10));
    QVERIFY(map.wrapEnabled());
    QCOMPARE(map.rowCount(), 4); // estimates until asked: one row per line
    const QList<DisplayRow> rows = allRows(map);
    QCOMPARE(rows.size(), 3 + 1 + 1 + 3);
    QCOMPARE(map.rowCount(), 8);
    QCOMPARE(rows[0].line, 0);
    QCOMPARE(rows[0].startColumn, 0);
    QCOMPARE(rows[0].endColumn, 6);
    QCOMPARE(rows[1].startColumn, 6);
    QCOMPARE(rows[1].endColumn, 16);
    QCOMPARE(rows[2].startColumn, 16);
    QCOMPARE(rows[2].endColumn, 19);
    QVERIFY(rows[0].isFirst() && !rows[0].isLast());
    QVERIFY(rows[2].isLast());
    QCOMPARE(rows[2].rowsInLine, 3);
    QCOMPARE(rows[3].line, 1);
    QCOMPARE(rows[4].line, 2);
    QCOMPARE(rows[4].endColumn, 0);
    QCOMPARE(rows[7].line, 3);
    QCOMPARE(rows[7].startColumn, 20);
    QCOMPARE(map.firstRowOfLine(3), 5);
    QCOMPARE(map.rowCountOfLine(0), 3);
    QCOMPARE(map.lineForRow(6), 3);
  }

  void positionsMapToRows() {
    TextDocument doc;
    doc.setText(u"hello world foo bar\nnext"_s);
    DisplayMap map(&doc);
    map.setWrapConfig(gridConfig(10));
    QCOMPARE(map.rowForPosition({0, 0}), 0);
    QCOMPARE(map.rowForPosition({0, 5}), 0);
    QCOMPARE(map.rowForPosition({0, 6}), 1); // a break belongs to the row after it
    QCOMPARE(map.rowForPosition({0, 15}), 1);
    QCOMPARE(map.rowForPosition({0, 16}), 2);
    QCOMPARE(map.rowForPosition({0, 19}), 2); // end of the line stays on the last row
    QCOMPARE(map.rowForPosition({1, 2}), 3);
  }

  void rowsAndPositionsRoundTrip() {
    QRandomGenerator rng(3);
    TextDocument doc;
    doc.setText(randomText(rng, 60, 80));
    DisplayMap map(&doc);
    map.setWrapConfig(gridConfig(14, true, true, 1));
    const QList<DisplayRow> rows = allRows(map);
    for (qsizetype r = 0; r < rows.size(); ++r) {
      const DisplayRow &row = rows[r];
      QCOMPARE(map.rowForPosition({row.line, row.startColumn}), r);
      if (row.endColumn > row.startColumn)
        QCOMPARE(map.rowForPosition({row.line, row.endColumn - 1}), r);
      // Rows of a line tile it.
      if (!row.isLast())
        QCOMPARE(rows[r + 1].startColumn, row.endColumn);
      else
        QCOMPARE(row.endColumn, doc.rope().lineLength(row.line));
      QCOMPARE(row.indent == 0, row.isFirst() || map.wrapConfig().rowWidth() / 2 == 0 || row.indent == 0);
    }
  }

  void estimatesAreRefinedAndReported() {
    TextDocument doc;
    doc.setText(u"hello world foo bar\nx"_s);
    DisplayMap map(&doc);
    map.setWrapConfig(gridConfig(10));
    QSignalSpy spy(&map, &DisplayMap::rowsReestimated);
    QCOMPARE(map.estimatedLineCount(), 2);
    QCOMPARE(map.rowCount(), 2);
    QCOMPARE(map.rowCountOfLine(0), 3);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst(), (QList<QVariant>{0, 1, 3}));
    QCOMPARE(map.rowCount(), 4);
    QCOMPARE(map.estimatedLineCount(), 1);
  }

  // ---- Edits ---------------------------------------------------------------------------------

  void editsMatchWrappingFromScratch() {
    QRandomGenerator rng(21);
    for (int round = 0; round < 8; ++round) {
      const WrapConfig config = gridConfig(6 + round * 3, round % 2, round % 3 == 0, round % 2);
      TextDocument doc;
      doc.setText(randomText(rng, 30, 40));
      DisplayMap map(&doc);
      map.setWrapConfig(config);
      allRows(map);
      QSignalSpy changed(&map, &DisplayMap::rowsChanged);
      for (int step = 0; step < 150; ++step) {
        const qsizetype rowsBefore = map.rowCount();
        const qsizetype length = doc.length();
        const qsizetype a = rng.bounded(length + 1);
        const qsizetype b = qMin<qsizetype>(length, a + rng.bounded(rng.bounded(6) == 0 ? 200 : 6));
        QString text = rng.bounded(3) == 0 ? QString() : randomText(rng, rng.bounded(3), 12);
        // Stay clear of CRLF pairs: their handling belongs to the document, not to wrapping.
        text.remove(u'\r');
        const quint64 version = doc.version();
        doc.replace(a, b, text);
        if (doc.version() == version) // nothing happened (empty replacing empty)
          continue;
        QCOMPARE(changed.count(), 1);
        const QList<QVariant> signal = changed.takeFirst();
        // Edited lines are wrapped on the spot and every other line was exact, so nothing is left to
        // estimate.
        QCOMPARE(map.estimatedLineCount(), 0);
        QCOMPARE(map.rowCount(), rowsBefore + signal[2].toLongLong() - signal[1].toLongLong());
        QVERIFY(signal[0].toLongLong() <= rowsBefore);

        TextDocument fresh;
        fresh.setText(doc.rope().toString());
        DisplayMap expected(&fresh);
        expected.setWrapConfig(config);
        const QList<DisplayRow> want = allRows(expected);
        const QList<DisplayRow> got = allRows(map);
        QCOMPARE(got.size(), want.size());
        for (qsizetype r = 0; r < got.size(); ++r) {
          QVERIFY2(
            got[r].line == want[r].line && got[r].startColumn == want[r].startColumn &&
              got[r].endColumn == want[r].endColumn && got[r].rowsInLine == want[r].rowsInLine &&
              qFuzzyCompare(got[r].indent + 1, want[r].indent + 1),
            qPrintable(u"round %1 step %2 row %3"_s.arg(round).arg(step).arg(r))
          );
        }
      }
    }
  }

  void typingInOneLineOnlyChangesItsRows() {
    TextDocument doc;
    doc.setText(u"hello world foo bar\nsecond line here\nthird"_s);
    DisplayMap map(&doc);
    map.setWrapConfig(gridConfig(10));
    allRows(map);
    QSignalSpy spy(&map, &DisplayMap::rowsChanged);
    doc.insert(doc.rope().lineStart(1), u"x"_s);
    QCOMPARE(spy.takeLast(), (QList<QVariant>{3, 2, 2}));
    doc.insert(doc.rope().lineStart(1), u"yyyyyyyyyyyyyyyyyyyyyyyyyy"_s);
    QCOMPARE(spy.takeLast(), (QList<QVariant>{3, 2, 5}));
    QCOMPARE(map.rowCount(), 3 + 5 + 1);
    // Lines after the edit moved down but kept their rows.
    QCOMPARE(map.rowAt(8).line, 2);
  }

  void newlinesSplitAndJoinRows() {
    TextDocument doc;
    doc.setText(u"hello world foo bar"_s);
    DisplayMap map(&doc);
    map.setWrapConfig(gridConfig(10));
    QCOMPARE(map.rowCountOfLine(0), 3);
    QSignalSpy spy(&map, &DisplayMap::rowsChanged);
    doc.insert(6, u"\n"_s);
    QCOMPARE(spy.takeLast(), (QList<QVariant>{0, 3, 3}));
    QCOMPARE(map.rowAt(0).endColumn, 6);
    QCOMPARE(map.rowAt(1).line, 1);
    doc.remove(6, 7);
    QCOMPARE(map.rowCountOfLine(0), 3);
    QCOMPARE(map.rowCount(), 3);
  }

  void bigInsertsAreLeftAsEstimates() {
    TextDocument doc;
    DisplayMap map(&doc);
    map.setWrapConfig(gridConfig(10));
    QString big;
    for (int i = 0; i < 40000; ++i)
      big += u"some words that wrap around\n"_s;
    doc.insert(0, big);
    QVERIFY(map.estimatedLineCount() > 1000);
    // Whatever is looked at is exact.
    QCOMPARE(map.rowAt(map.rowCount() - 1).line, 40000);
    QCOMPARE(map.rowCountOfLine(500), 3);
  }

  // ---- Background wrapping -------------------------------------------------------------------

  static void expectSameRows(DisplayMap &map, const TextDocument &doc, const WrapConfig &config) {
    TextDocument fresh;
    fresh.setText(doc.rope().toString());
    DisplayMap expected(&fresh);
    expected.setBackgroundWrapping(false);
    expected.setWrapConfig(config);
    const QList<DisplayRow> want = allRows(expected);
    const QList<DisplayRow> got = allRows(map);
    QCOMPARE(got.size(), want.size());
    for (qsizetype r = 0; r < got.size(); ++r)
      QVERIFY2(
        got[r].line == want[r].line && got[r].startColumn == want[r].startColumn &&
          got[r].endColumn == want[r].endColumn && got[r].rowsInLine == want[r].rowsInLine &&
          qFuzzyCompare(got[r].indent + 1, want[r].indent + 1),
        qPrintable(u"row %1"_s.arg(r))
      );
  }

  void backgroundWrappingConverges() {
    QRandomGenerator rng(8);
    TextDocument doc;
    doc.setText(randomText(rng, 6000, 60));
    DisplayMap map(&doc);
    QSignalSpy progress(&map, &DisplayMap::wrapProgress);
    QSignalSpy refined(&map, &DisplayMap::rowsReestimated);
    const WrapConfig config = gridConfig(12, true, true);
    map.setWrapConfig(config);
    QCOMPARE(map.estimatedLineCount(), 6000);
    QTRY_COMPARE_WITH_TIMEOUT(map.estimatedLineCount(), 0, 20000);
    QVERIFY(progress.count() >= 1);
    QCOMPARE(progress.last().first().toLongLong(), 0);
    QVERIFY(refined.count() >= 1);
    expectSameRows(map, doc, config);
  }

  void editsWhileWrappingInTheBackground() {
    QRandomGenerator rng(9);
    TextDocument doc;
    doc.setText(randomText(rng, 8000, 40));
    DisplayMap map(&doc);
    const WrapConfig config = gridConfig(9);
    map.setWrapConfig(config);
    for (int step = 0; step < 40; ++step) {
      const qsizetype length = doc.length();
      const qsizetype a = rng.bounded(length + 1);
      QString text = randomText(rng, rng.bounded(3), 10);
      text.remove(u'\r');
      doc.replace(a, qMin<qsizetype>(length, a + rng.bounded(30)), text);
      QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    QTRY_COMPARE_WITH_TIMEOUT(map.estimatedLineCount(), 0, 20000);
    expectSameRows(map, doc, config);
  }

  void changingTheConfigDropsStaleChunks() {
    QRandomGenerator rng(10);
    TextDocument doc;
    doc.setText(randomText(rng, 4000, 50));
    DisplayMap map(&doc);
    map.setWrapConfig(gridConfig(8));
    map.setWrapConfig(gridConfig(20));
    QTRY_COMPARE_WITH_TIMEOUT(map.estimatedLineCount(), 0, 20000);
    expectSameRows(map, doc, gridConfig(20));
  }

  void turningWrapOffRestoresIdentity() {
    TextDocument doc;
    doc.setText(u"hello world foo bar\nx"_s);
    DisplayMap map(&doc);
    map.setWrapConfig(gridConfig(10));
    QCOMPARE(allRows(map).size(), 4);
    QSignalSpy spy(&map, &DisplayMap::reset);
    map.setWrapConfig({});
    QCOMPARE(spy.count(), 1);
    QVERIFY(!map.wrapEnabled());
    QCOMPARE(map.rowCount(), 2);
    QCOMPARE(map.rowAt(0).endColumn, 19);
  }

};

QTEST_GUILESS_MAIN(TstWrapMap)
#include "tst_wrapmap.moc"
