#include "core/highlighter.h"
#include "core/textdocument.h"

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
};

QTEST_APPLESS_MAIN(TstHighlighter)
#include "tst_highlighter.moc"
