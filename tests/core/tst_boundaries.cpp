#include "core/textboundaries.h"
#include "testutil.h"

#include <QtCore/QTextBoundaryFinder>
#include <QtTest>

using namespace qce;
using namespace Qt::StringLiterals;

namespace {

QString graphemeText(test::Random &rnd, int pieces) {
  static const QString parts[] = {
    QStringLiteral("a"),
    QStringLiteral("b"),
    QStringLiteral(" "),
    QStringLiteral("\n"),
    QStringLiteral("\r\n"),
    QStringLiteral("é"),
    QStringLiteral("é"),
    QStringLiteral("\U0001F600"),
    QStringLiteral("\U0001F468‍\U0001F469‍\U0001F467"), // family: ZWJ sequence
    QStringLiteral("\U0001F1EF\U0001F1F5"),                 // flag
    QStringLiteral("\U0001F44D\U0001F3FD"),                 // skin tone modifier
    QStringLiteral("x̀́̂"),                                    // stacked marks
  };
  QString out;
  for (int i = 0; i < pieces; ++i)
    out += parts[rnd.below(int(std::size(parts)))];
  return out;
}

QList<qsizetype> referenceBoundaries(const QString &text) {
  QList<qsizetype> b;
  QTextBoundaryFinder f(QTextBoundaryFinder::Grapheme, text);
  b << 0;
  for (int p = f.toNextBoundary(); p >= 0; p = f.toNextBoundary())
    b << p;
  return b;
}

} // namespace

class TstBoundaries : public QObject {
  Q_OBJECT
private slots:
  void codePoints() {
    const TextBoundaries b(Rope::fromString(u"a\U0001F600b"));
    QCOMPARE(b.nextCodePoint(0), 1);
    QCOMPARE(b.nextCodePoint(1), 3);
    QCOMPARE(b.nextCodePoint(3), 4);
    QCOMPARE(b.nextCodePoint(4), 4);
    QCOMPARE(b.previousCodePoint(4), 3);
    QCOMPARE(b.previousCodePoint(3), 1);
    QCOMPARE(b.previousCodePoint(1), 0);
    QCOMPARE(b.previousCodePoint(0), 0);
  }

  void graphemeExamples() {
    const QString family = QStringLiteral("\U0001F468‍\U0001F469‍\U0001F467");
    const TextBoundaries b(Rope::fromString(u"a" + family + u"é\r\nz"));
    QCOMPARE(b.nextGrapheme(0), 1);
    QCOMPARE(b.nextGrapheme(1), 1 + family.size());
    const qsizetype afterFamily = 1 + family.size();
    QCOMPARE(b.nextGrapheme(afterFamily), afterFamily + 2);     // e + combining acute
    QCOMPARE(b.nextGrapheme(afterFamily + 2), afterFamily + 4); // CRLF is one cluster
    QCOMPARE(b.previousGrapheme(afterFamily + 4), afterFamily + 2);
    QCOMPARE(b.previousGrapheme(afterFamily + 2), afterFamily);
    QCOMPARE(b.previousGrapheme(afterFamily), 1);
    QCOMPARE(b.previousGrapheme(1), 0);
  }

  void graphemesMatchTextBoundaryFinder() {
    test::Random rnd(test::testSeed());
    for (int round = 0; round < test::testIterations(12); ++round) {
      // sizes up to ~20k units so windows, leaf boundaries and widening are all exercised
      const QString text = graphemeText(rnd, round < 8 ? rnd.range(1, 400) : 6000);
      const Rope rope = Rope::fromString(text);
      const TextBoundaries b(rope);
      const QList<qsizetype> ref = referenceBoundaries(text);
      for (qsizetype i = 0; i + 1 < ref.size(); ++i) {
        if (i % (round < 8 ? 1 : 7) != 0)
          continue;
        QCOMPARE(b.nextGrapheme(ref[i]), ref[i + 1]);
        QCOMPARE(b.previousGrapheme(ref[i + 1]), ref[i]);
      }
    }
  }

  void longClusterWidensTheWindow() {
    // 600 combining marks after one base: longer than the initial window on either side
    const QString text = QStringLiteral("a") + QString(600, QChar(0x0301)) + QStringLiteral("b");
    const TextBoundaries b(Rope::fromString(text));
    QCOMPARE(b.nextGrapheme(0), 601);
    QCOMPARE(b.previousGrapheme(601), 0);
  }

  void wordMotions() {
    //            0123456789012345678
    const QString t = u"foo bar_baz.qux  x"_s;
    const TextBoundaries b(Rope::fromString(t));
    QCOMPARE(b.nextWordStart(0), 4);
    QCOMPARE(b.nextWordStart(4), 11); // bar_baz ends at '.', which is its own word
    QCOMPARE(b.nextWordStart(11), 12);
    QCOMPARE(b.nextWordStart(12), 17);
    QCOMPARE(b.nextWordStart(17), 18);
    QCOMPARE(b.nextWordStart(18), 18);
    QCOMPARE(b.nextWordStart(4, true), 17); // WORD: bar_baz.qux is one
    QCOMPARE(b.previousWordStart(18), 17);
    QCOMPARE(b.previousWordStart(17), 12);
    QCOMPARE(b.previousWordStart(12), 11);
    QCOMPARE(b.previousWordStart(11), 4);
    QCOMPARE(b.previousWordStart(5), 4);
    QCOMPARE(b.previousWordStart(4), 0);
    QCOMPARE(b.previousWordStart(0), 0);
    QCOMPARE(b.previousWordStart(17, true), 4);
    QCOMPARE(b.nextWordEnd(0), 3);
    QCOMPARE(b.nextWordEnd(3), 11);
    QCOMPARE(b.nextWordEnd(1), 3);
    QCOMPARE(b.nextWordEnd(11), 12);
    QCOMPARE(b.nextWordEnd(12), 15);
    QCOMPARE(b.nextWordEnd(15), 18);
    QCOMPARE(b.nextWordEnd(18), 18);
    QCOMPARE(b.nextWordEnd(3, true), 15);
    QCOMPARE(b.previousWordEnd(18), 15);
    QCOMPARE(b.previousWordEnd(15), 12);
    QCOMPARE(b.previousWordEnd(12), 11);
    QCOMPARE(b.previousWordEnd(11), 3);
    QCOMPARE(b.previousWordEnd(4), 3);
    QCOMPARE(b.previousWordEnd(3), 0);
    QCOMPARE(b.previousWordEnd(18, true), 15);
  }

  void wordRanges() {
    const QString t = u"foo bar_baz.qux"_s;
    const TextBoundaries b(Rope::fromString(t));
    QCOMPARE(b.wordRangeAt(1), (QPair<qsizetype, qsizetype>{0, 3}));
    QCOMPARE(b.wordRangeAt(3), (QPair<qsizetype, qsizetype>{3, 4})); // the space
    QCOMPARE(b.wordRangeAt(6), (QPair<qsizetype, qsizetype>{4, 11}));
    QCOMPARE(b.wordRangeAt(11), (QPair<qsizetype, qsizetype>{11, 12}));
    QCOMPARE(b.wordRangeAt(15), (QPair<qsizetype, qsizetype>{12, 15})); // at the end: previous unit
    QCOMPARE(b.wordRangeAt(6, true), (QPair<qsizetype, qsizetype>{4, 15}));
    QCOMPARE(TextBoundaries(Rope()).wordRangeAt(0), (QPair<qsizetype, qsizetype>{0, 0}));
  }

  void wordsHandleSurrogatesAndUnicodeLetters() {
    // é is a letter, the emoji is punctuation/symbol, CJK counts as word
    const QString t = u"café \U0001F600\U0001F600 世界"_s;
    const TextBoundaries b(Rope::fromString(t));
    QCOMPARE(b.nextWordStart(0), 5);
    QCOMPARE(b.nextWordStart(5), 10);
    QCOMPARE(b.wordRangeAt(5), (QPair<qsizetype, qsizetype>{5, 9}));
    QCOMPARE(b.previousWordStart(9), 5);
    QCOMPARE(b.previousWordStart(12), 10);
  }
};

QTEST_APPLESS_MAIN(TstBoundaries)
#include "tst_boundaries.moc"
