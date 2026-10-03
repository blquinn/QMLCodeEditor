#include "syntax/treesitterhighlighter.h"

#include <QtTest/QtTest>

using namespace qce;
using namespace Qt::StringLiterals;

namespace {

void settle(TreeSitterHighlighter &h) {
  QTRY_VERIFY_WITH_TIMEOUT(h.stats().landed >= 1 && !h.parsing(), 20000);
}

QList<FoldRange> rangesFor(const QString &fileName, const QString &text) {
  TextDocument doc;
  doc.setText(text);
  TreeSitterHighlighter h;
  h.setFileName(fileName);
  h.attach(&doc);
  settle(h);
  return h.folds()->foldRanges(doc.snapshot(), 0, doc.rope().lineCount() - 1);
}

} // namespace

class TstFoldQuery : public QObject {
  Q_OBJECT
private slots:
  void everyFoldQueryCompiles() {
    for (const LanguageInfo &info : LanguageRegistry::instance().languages()) {
      if (info.foldQueries.isEmpty())
        continue;
      const auto compiled = LanguageRegistry::instance().compiled(info.id);
      QVERIFY2(compiled && compiled->folds, qPrintable(info.id));
    }
  }

  void jsonObjectsAndArrays() {
    const QString json = u"{\n"            // 0
                         u"  \"a\": [\n"   // 1
                         u"    1,\n"       // 2
                         u"    2\n"        // 3
                         u"  ],\n"         // 4
                         u"  \"b\": {\"c\": 1},\n" // 5
                         u"  \"d\": {\n"   // 6
                         u"    \"e\": 2\n" // 7
                         u"  }\n"          // 8
                         u"}\n"_s;         // 9
    // The line with the closing token stays visible; single-line values have nothing to hide.
    QCOMPARE(rangesFor(u"a.json"_s, json), (QList<FoldRange>{{0, 8}, {1, 3}, {6, 7}}));
  }

  void javascriptBlocks() {
    const QString js = u"class A {\n"            // 0
                       u"  f(a,\n"               // 1
                       u"    b) {\n"             // 2
                       u"    if (a) {\n"         // 3
                       u"      g();\n"           // 4
                       u"    } else {\n"         // 5
                       u"      h();\n"           // 6
                       u"    }\n"                // 7
                       u"  }\n"                  // 8
                       u"}\n"_s;                 // 9
    const QList<FoldRange> ranges = rangesFor(u"a.js"_s, js);
    QVERIFY(ranges.contains({0, 8}));
    QVERIFY(ranges.contains({2, 7}));
    QVERIFY(ranges.contains({3, 4}));
    QVERIFY(ranges.contains({5, 6}));
    QVERIFY(ranges.contains({1, 1 + 0} ) == false);
    // Sorted, one per header.
    qsizetype previous = -1;
    for (const FoldRange &r : ranges) {
      QVERIFY(r.startLine > previous);
      previous = r.startLine;
    }
  }

  void htmlElements() {
    const QString html = u"<div>\n"           // 0
                         u"  <p>\n"           // 1
                         u"    text\n"        // 2
                         u"  </p>\n"          // 3
                         u"  <b>x</b>\n"      // 4
                         u"</div>\n"_s;       // 5
    QCOMPARE(rangesFor(u"a.html"_s, html), (QList<FoldRange>{{0, 4}, {1, 2}}));
  }

  void markdownSectionsAndFences() {
    const QString md = u"# One\n"          // 0
                       u"text\n"           // 1
                       u"## Two\n"         // 2
                       u"```js\n"          // 3
                       u"let a;\n"         // 4
                       u"```\n"            // 5
                       u"## Three\n"       // 6
                       u"more\n"_s;        // 7
    const QList<FoldRange> ranges = rangesFor(u"a.md"_s, md);
    QVERIFY(ranges.contains({0, 7}));
    QVERIFY(ranges.contains({2, 5}));
    QVERIFY(ranges.contains({3, 4}));
    QVERIFY(ranges.contains({6, 7}));
  }

  void scriptInHtmlFoldsThroughTheInjectedLayer() {
    const QString html = u"<script>\n"      // 0
                         u"function f() {\n" // 1
                         u"  g();\n"       // 2
                         u"}\n"            // 3
                         u"</script>\n"_s; // 4
    const QList<FoldRange> ranges = rangesFor(u"a.html"_s, html);
    QVERIFY(ranges.contains({0, 3}));
    QVERIFY(ranges.contains({1, 2}));
  }

  void plainTextFallsBackToIndentation() {
    TextDocument doc;
    doc.setText(u"a\n  b\n  c\nd"_s);
    TreeSitterHighlighter h;
    h.setLanguage(u"plain"_s);
    h.attach(&doc);
    QCOMPARE(h.folds()->foldRanges(doc.snapshot(), 0, 3), (QList<FoldRange>{{0, 2}}));
  }

  void beforeTheFirstParseTheFallbackAnswers() {
    TextDocument doc;
    doc.setText(u"{\n  \"a\": 1\n}\n"_s);
    TreeSitterHighlighter h;
    h.setFileName(u"a.json"_s);
    h.attach(&doc);
    // Whatever is available, asking is safe and gives a range for the object.
    QVERIFY(!h.folds()->foldRanges(doc.snapshot(), 0, 2).isEmpty());
  }

  void linesOutsideAWindowedParseUseIndentation() {
    QString text = u"function f() {\n"_s;
    for (int i = 0; i < 4000; ++i)
      text += u"  a(%1);\n"_s.arg(i);
    text += u"}\n"_s;
    for (int i = 0; i < 40; ++i)
      text += u"if (x) {\n  y();\n}\n"_s;
    TextDocument doc;
    doc.setText(text);
    TreeSitterHighlighter h;
    h.setFileName(u"a.js"_s);
    h.setFullParseLimit(1000);
    h.setWindowSize(2000);
    h.attach(&doc);
    settle(h);
    QVERIFY(!h.hasFullTree());
    // Near the top the tree answers: the block ends before the closing brace.
    const QList<FoldRange> top = h.folds()->foldRanges(doc.snapshot(), 0, 0);
    QVERIFY(top.isEmpty() || top.first().startLine == 0);
    // Far outside the window the fallback finds the indented blocks.
    const qsizetype lastLine = doc.rope().lineCount() - 1;
    const QList<FoldRange> bottom = h.folds()->foldRanges(doc.snapshot(), lastLine - 30, lastLine);
    QVERIFY(!bottom.isEmpty());
  }

  void invalidatedWhenAParseLands() {
    TextDocument doc;
    doc.setText(u"{\n  \"a\": 1\n}\n"_s);
    TreeSitterHighlighter h;
    h.setFileName(u"a.json"_s);
    QSignalSpy spy(h.folds(), &FoldProvider::invalidated);
    h.attach(&doc);
    settle(h);
    QVERIFY(spy.count() >= 1);
  }
};

QTEST_MAIN(TstFoldQuery)
#include "tst_foldquery.moc"
