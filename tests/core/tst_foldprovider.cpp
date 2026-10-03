#include "core/folding.h"

#include <QtTest>

using namespace qce;
using namespace Qt::StringLiterals;

namespace {

const QString kCode = u"class A {\n"       // 0
                      u"  void f() {\n"    // 1
                      u"    one();\n"      // 2
                      u"\n"                // 3
                      u"    two();\n"      // 4
                      u"  }\n"             // 5
                      u"\n"                // 6
                      u"  void g() {\n"    // 7
                      u"    three();\n"    // 8
                      u"  }\n"             // 9
                      u"}\n"               // 10
                      u"tail"_s;           // 11

QList<FoldRange> rangesOf(const QString &text, qsizetype first = 0, qsizetype last = 1 << 20) {
  TextDocument doc;
  doc.setText(text);
  IndentFoldProvider provider;
  return provider.foldRanges(doc.snapshot(), first, last);
}

} // namespace

class TstFoldProvider : public QObject {
  Q_OBJECT
private slots:
  void indentRangesStopBeforeTheClosingLine() {
    const QList<FoldRange> want = {{0, 9}, {1, 4}, {7, 8}};
    QCOMPARE(rangesOf(kCode), want);
  }

  void onlyHeadersInTheRequestedLinesAreReturned() {
    QCOMPARE(rangesOf(kCode, 1, 1), (QList<FoldRange>{{1, 4}}));
    QCOMPARE(rangesOf(kCode, 2, 6), QList<FoldRange>{});
    QCOMPARE(rangesOf(kCode, 7, 11), (QList<FoldRange>{{7, 8}}));
  }

  void blankLinesDoNotEndOrExtendRanges() {
    QCOMPARE(rangesOf(u"a\n\n\n  b\n\n  c\n\n\nd"_s), (QList<FoldRange>{{0, 5}}));
    QCOMPARE(rangesOf(u"a\n  b\n\n\n"_s), (QList<FoldRange>{{0, 1}}));
    QCOMPARE(rangesOf(u"a\n\n"_s), QList<FoldRange>{});
  }

  void tabsCountAsTabWidthColumns() {
    // A tab is four columns, deeper than two spaces.
    QCOMPARE(rangesOf(u"\ta\n  b"_s), QList<FoldRange>{});
    QCOMPARE(rangesOf(u"\ta\n\t\tb"_s), (QList<FoldRange>{{0, 1}}));
    QCOMPARE(rangesOf(u"  a\n\tb"_s), (QList<FoldRange>{{0, 1}}));
  }

  void crlfAndWhitespaceOnlyLines() {
    QCOMPARE(rangesOf(u"a\r\n  b\r\n   \r\n  c\r\nd"_s), (QList<FoldRange>{{0, 3}}));
  }

  void rangesToTheEndOfTheText() {
    QCOMPARE(rangesOf(u"a\n  b\n  c"_s), (QList<FoldRange>{{0, 2}}));
    QCOMPARE(rangesOf(u"a\n  b\n    c\n  d"_s), (QList<FoldRange>{{0, 3}, {1, 2}}));
  }

  void rangesLongerThanTheScanLimitAreNotOffered() {
    QString text = u"head\n"_s;
    for (int i = 0; i < 3000; ++i)
      text += u"  body\n"_s;
    text += u"end"_s;
    TextDocument doc;
    doc.setText(text);
    IndentFoldProvider provider;
    provider.setMaxScanLines(100);
    QVERIFY(provider.foldRanges(doc.snapshot(), 0, 10).isEmpty());
    provider.setMaxScanLines(5000);
    QCOMPARE(provider.foldRanges(doc.snapshot(), 0, 10), (QList<FoldRange>{{0, 3000}}));
  }

  void largeRequestsAreExact() {
    QString text;
    for (int i = 0; i < 3000; ++i)
      text += u"a\n  b\n    c\n  d\n"_s;
    const QList<FoldRange> ranges = rangesOf(text);
    QCOMPARE(ranges.size(), 6000);
    QCOMPARE(ranges[0], (FoldRange{0, 3}));
    QCOMPARE(ranges[1], (FoldRange{1, 2}));
    QCOMPARE(ranges[2], (FoldRange{4, 7}));
  }

  void blocksFollowEdits() {
    TextDocument doc;
    doc.setText(kCode);
    IndentFoldProvider provider;
    QCOMPARE(provider.foldRanges(doc.snapshot(), 0, 3).size(), 2);
    doc.insert(0, u"x\n"_s);
    QCOMPARE(provider.foldRanges(doc.snapshot(), 0, 3), (QList<FoldRange>{{1, 10}, {2, 5}}));
  }

  void depths() {
    const QList<FoldRange> ranges = {{0, 9}, {1, 4}, {2, 3}, {7, 8}, {12, 14}};
    QCOMPARE(foldDepths(ranges), (QList<int>{1, 2, 3, 2, 1}));
  }

  void endLineKeepsClosingTokensVisible() {
    const Rope rope = Rope::fromString(u"x {\n  a;\n}\ny {\n  b; }\nz\n```\ntext\n```\n<div>\n</div>\n/*\n */"_s);
    QCOMPARE(foldEndLine(rope, 2, 1), 1);  // "}"
    QCOMPARE(foldEndLine(rope, 4, 6), 4);  // "  b; }" holds code before the brace
    QCOMPARE(foldEndLine(rope, 8, 3), 7);  // closing fence
    QCOMPARE(foldEndLine(rope, 10, 6), 9); // </div>
    QCOMPARE(foldEndLine(rope, 12, 3), 11); // */
    QCOMPARE(foldEndLine(rope, 5, 0), 4);  // ends at the start of the next line
  }

  // ---- Commands -----------------------------------------------------------------------------

  void foldAtCursorFoldsTheInnermostRangeThenItsParent() {
    TextDocument doc;
    doc.setText(kCode);
    DisplayMap map(&doc);
    IndentFoldProvider provider;
    const TextSnapshot text = doc.snapshot();
    QVERIFY(folding::foldAt(map, provider, text, 2));
    QVERIFY(map.folds().isFolded(1));
    QVERIFY(!map.folds().isFolded(0));
    // The cursor would now be on the header; folding again goes outwards.
    QVERIFY(folding::foldAt(map, provider, text, 1));
    QVERIFY(map.folds().isFolded(0));
    QVERIFY(!folding::foldAt(map, provider, text, 0));
    QVERIFY(!folding::foldAt(map, provider, text, 11)); // no range around "tail"
  }

  void toggleFoldsAndUnfoldsAHeader() {
    TextDocument doc;
    doc.setText(kCode);
    DisplayMap map(&doc);
    IndentFoldProvider provider;
    const TextSnapshot text = doc.snapshot();
    QVERIFY(folding::toggle(map, provider, text, 7));
    QCOMPARE(map.rowCount(), 11);
    QVERIFY(folding::toggle(map, provider, text, 7));
    QCOMPARE(map.rowCount(), 12);
    QVERIFY(!folding::toggle(map, provider, text, 2)); // not a header
  }

  void foldAllAndUnfoldAll() {
    TextDocument doc;
    doc.setText(kCode);
    DisplayMap map(&doc);
    IndentFoldProvider provider;
    const TextSnapshot text = doc.snapshot();
    QVERIFY(folding::foldAll(map, provider, text));
    QCOMPARE(map.folds().folds(), (QList<FoldRange>{{0, 9}, {1, 4}, {7, 8}}));
    QCOMPARE(map.rowCount(), 3); // "class A {", "}", "tail"
    QVERIFY(!folding::foldAll(map, provider, text));
    QVERIFY(folding::unfoldAll(map));
    QCOMPARE(map.rowCount(), 12);
    QVERIFY(!folding::unfoldAll(map));
  }

  void foldToLevel() {
    TextDocument doc;
    doc.setText(kCode);
    DisplayMap map(&doc);
    IndentFoldProvider provider;
    const TextSnapshot text = doc.snapshot();
    QVERIFY(folding::foldToLevel(map, provider, text, 1));
    QCOMPARE(map.folds().folds(), (QList<FoldRange>{{0, 9}}));
    QVERIFY(folding::foldToLevel(map, provider, text, 2));
    QCOMPARE(map.folds().folds(), (QList<FoldRange>{{1, 4}, {7, 8}}));
    QCOMPARE(map.rowCount(), 8);
  }

  void unfoldAtHeaderAndInside() {
    TextDocument doc;
    doc.setText(kCode);
    DisplayMap map(&doc);
    map.setFolds({{0, 9}, {1, 4}});
    QVERIFY(folding::unfoldAt(map, 0));
    QVERIFY(map.folds().isFolded(1)); // the inner one stays
    QVERIFY(!folding::unfoldAt(map, 7));
    QVERIFY(folding::unfoldAt(map, 1));
    QCOMPARE(map.rowCount(), 12);
  }
};

QTEST_GUILESS_MAIN(TstFoldProvider)
#include "tst_foldprovider.moc"
