#include "core/highlighter.h"
#include "core/textdocument.h"

#include <QtConcurrent/QtConcurrentRun>
#include <QtTest>

using namespace Qt::StringLiterals;

using namespace qce;

class TstHighlighter : public QObject {
  Q_OBJECT
private slots:
  void nullHighlighterReturnsEmptyLinePerLine() {
    TextDocument doc;
    doc.setText(u"a\nb\nc"_s);
    NullHighlighter h;
    const auto all = h.highlightLines(doc.snapshot(), 0, 2);
    QCOMPARE(all.size(), 3);
    for (const auto &spans : all)
      QVERIFY(spans.isEmpty());
    QCOMPARE(h.highlightLines(doc.snapshot(), 1, 99).size(), 2); // clamped to the document
    QCOMPARE(h.highlightLines(doc.snapshot(), 5, 9).size(), 0);
  }

  void styleNames() {
    QCOMPARE(tokenStyleName(TokenStyle::Keyword), "keyword"_L1);
    QCOMPARE(tokenStyleName(TokenStyle::Preprocessor), "preprocessor"_L1);
    QVERIFY(tokenStyleName(TokenStyle::Default).isEmpty());
    for (int i = 1; i < int(TokenStyle::Count); ++i)
      QVERIFY(!tokenStyleName(TokenStyle(i)).isEmpty());
  }

  void builtinNamesResolveToBuiltins() {
    QCOMPARE(tokenStyleFromName(u"Keyword"), std::optional(TokenStyle::Keyword));
    QCOMPARE(registerTokenStyle(u"link"), TokenStyle::Link);
    QVERIFY(!isCustomTokenStyle(registerTokenStyle(u"heading")));
    QVERIFY(!tokenStyleFromName(u""));
    QVERIFY(!tokenStyleFromName(u"no.such.style.yet"));
    QCOMPARE(registerTokenStyle(u"  "), TokenStyle::Default);
  }

  void customStylesAreRegisteredOnce() {
    const TokenStyle a = registerTokenStyle(u"Variable.Defined");
    QVERIFY(isCustomTokenStyle(a));
    QCOMPARE(registerTokenStyle(u"variable.defined"), a);
    QCOMPARE(tokenStyleFromName(u"VARIABLE.DEFINED"), std::optional(a));
    QCOMPARE(tokenStyleName(a), u"variable.defined"_s);
    const TokenStyle b = registerTokenStyle(u"variable.undefined");
    QVERIFY(b != a);
    QVERIFY(customTokenStyleNames().contains(u"variable.undefined"_s));
  }

  void registryIsThreadSafe() {
    QList<QFuture<TokenStyle>> jobs;
    for (int i = 0; i < 8; ++i)
      jobs.append(QtConcurrent::run([] {
        TokenStyle last = TokenStyle::Default;
        for (int n = 0; n < 40; ++n)
          last = registerTokenStyle(u"threaded.%1"_s.arg(n % 20));
        return last;
      }));
    for (auto &job : jobs)
      job.waitForFinished();
    for (int n = 0; n < 20; ++n)
      QVERIFY(tokenStyleFromName(u"threaded.%1"_s.arg(n)));
    int count = 0; // each name exactly once
    for (const QString &name : customTokenStyleNames())
      count += name.startsWith(u"threaded."_s);
    QCOMPARE(count, 20);
  }

  void overlaySpansTable_data() {
    using L = QList<HighlightSpan>;
    const auto K = TokenStyle::Keyword, S = TokenStyle::String, V = TokenStyle::Variable;
    QTest::addColumn<L>("base");
    QTest::addColumn<L>("overlay");
    QTest::addColumn<L>("expected");
    QTest::newRow("no overlay") << L{{0, 5, K}} << L{} << L{{0, 5, K}};
    QTest::newRow("no base") << L{} << L{{2, 3, V}} << L{{2, 3, V}};
    QTest::newRow("disjoint") << L{{0, 2, K}, {8, 2, S}} << L{{4, 2, V}} << L{{0, 2, K}, {4, 2, V}, {8, 2, S}};
    QTest::newRow("inside a span") << L{{0, 10, S}} << L{{3, 4, V}} << L{{0, 3, S}, {3, 4, V}, {7, 3, S}};
    QTest::newRow("swallows spans") << L{{2, 2, K}, {5, 2, S}} << L{{1, 8, V}} << L{{1, 8, V}};
    QTest::newRow("straddles the start") << L{{0, 6, S}} << L{{4, 4, V}} << L{{0, 4, S}, {4, 4, V}};
    QTest::newRow("straddles the end") << L{{4, 6, S}} << L{{2, 5, V}} << L{{2, 5, V}, {7, 3, S}};
    QTest::newRow("adjacent") << L{{0, 4, K}, {4, 4, S}} << L{{4, 2, V}} << L{{0, 4, K}, {4, 2, V}, {6, 2, S}};
    QTest::newRow("two overlays in one span") << L{{0, 12, S}} << L{{1, 2, V}, {6, 2, K}}
                                              << L{{0, 1, S}, {1, 2, V}, {3, 3, S}, {6, 2, K}, {8, 4, S}};
    QTest::newRow("empty and default are skipped") << L{{0, 6, S}} << L{{1, 0, V}, {2, 2, TokenStyle::Default}}
                                                   << L{{0, 6, S}};
  }
  void overlaySpansTable() {
    QFETCH(QList<HighlightSpan>, base);
    QFETCH(QList<HighlightSpan>, overlay);
    QFETCH(QList<HighlightSpan>, expected);
    QCOMPARE(overlaySpans(base, overlay), expected);
  }

  // Keep last: it takes every remaining slot.
  void slotsRunOut() {
    TokenStyle last = TokenStyle::Default;
    for (int i = 0; i < kMaxCustomTokenStyles + 5; ++i)
      last = registerTokenStyle(u"fill.%1"_s.arg(i));
    QCOMPARE(last, TokenStyle::Default);
    QCOMPARE(customTokenStyleNames().size(), qsizetype(kMaxCustomTokenStyles));
    // Names that exist still resolve.
    QVERIFY(isCustomTokenStyle(registerTokenStyle(u"variable.defined")));
  }
};

QTEST_APPLESS_MAIN(TstHighlighter)
#include "tst_highlighter.moc"
