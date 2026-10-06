#include "core/bracketmatch.h"
#include "core/rope.h"

#include <QtTest>

using namespace qce;
using namespace Qt::StringLiterals;

namespace {

const BracketPairs kPairs = {{u'(', u')'}, {u'[', u']'}, {u'{', u'}'}, {u'"', u'"'}};

BracketPair match(const QString &text, qsizetype offset, const BracketPairs &pairs = kPairs,
                  qsizetype maxScan = kBracketScanLimit) {
  return findMatchingBracket(Rope::fromString(text), offset, pairs, maxScan);
}

} // namespace

class TstBracketMatch : public QObject {
  Q_OBJECT
private slots:
  void matches_data() {
    QTest::addColumn<QString>("text");
    QTest::addColumn<int>("offset");
    QTest::addColumn<int>("open");
    QTest::addColumn<int>("close");
    QTest::newRow("forward") << u"(a)"_s << 0 << 0 << 2;
    QTest::newRow("backward") << u"(a)"_s << 2 << 0 << 2;
    QTest::newRow("nested outer") << u"(a(b)c)"_s << 0 << 0 << 6;
    QTest::newRow("nested inner") << u"(a(b)c)"_s << 2 << 2 << 4;
    QTest::newRow("nested closer") << u"(a(b)c)"_s << 6 << 0 << 6;
    QTest::newRow("empty pair") << u"()"_s << 1 << 0 << 1;
    QTest::newRow("across lines") << u"{\n  [1,\n  2]\n}"_s << 0 << 0 << 13;
    QTest::newRow("other kinds are ignored") << u"( ] [ ) )"_s << 0 << 0 << 6;
    QTest::newRow("surrogates between") << u"(\U0001F600)"_s << 3 << 0 << 3;
    QTest::newRow("not a bracket") << u"(a)"_s << 1 << -1 << -1;
    QTest::newRow("quotes are no brackets") << u"\"a\""_s << 0 << -1 << -1;
    QTest::newRow("unmatched opener") << u"(a"_s << 0 << -1 << -1;
    QTest::newRow("unmatched closer") << u"a)"_s << 1 << -1 << -1;
    QTest::newRow("out of range") << u"()"_s << 5 << -1 << -1;
  }
  void matches() {
    QFETCH(QString, text);
    QFETCH(int, offset);
    QFETCH(int, open);
    QFETCH(int, close);
    const BracketPair pair = match(text, offset);
    QCOMPARE(pair.open, qsizetype(open));
    QCOMPARE(pair.close, qsizetype(close));
    QCOMPARE(pair.valid(), open >= 0);
  }

  void customPairs() {
    const BracketPairs angle = {{u'<', u'>'}};
    QCOMPARE(match(u"<a<b>>"_s, 0, angle).close, qsizetype(5));
    QVERIFY(!match(u"(a)"_s, 0, angle).valid());
  }

  void scanLimit() {
    const QString text = u"("_s + QString(50, u'x') + u")"_s;
    QVERIFY(match(text, 0, kPairs, 100).valid());
    QVERIFY(match(text, 51, kPairs, 100).valid());
    QVERIFY(!match(text, 0, kPairs, 10).valid());
    QVERIFY(!match(text, 51, kPairs, 10).valid());
    // Exactly reachable.
    QVERIFY(match(text, 0, kPairs, 51).valid());
    QVERIFY(!match(text, 0, kPairs, 50).valid());
  }

  void longDocuments() {
    // Match across many rope leaves and several backward blocks.
    const QString filler = QString(40000, u'x');
    const QString text = u"{"_s + filler + u"[()]"_s + filler + u"}"_s;
    const qsizetype last = text.size() - 1;
    QCOMPARE(match(text, 0).close, last);
    QCOMPARE(match(text, last).open, qsizetype(0));
    QCOMPARE(match(text, 40001).close, qsizetype(40004));
  }

  void nearCursor() {
    const Rope rope = Rope::fromString(u"a(b)c"_s);
    QCOMPARE(bracketNearCursor(rope, 1, kPairs), qsizetype(1)); // before "("
    QCOMPARE(bracketNearCursor(rope, 2, kPairs), qsizetype(1)); // after "("
    QCOMPARE(bracketNearCursor(rope, 3, kPairs), qsizetype(3)); // before ")"
    QCOMPARE(bracketNearCursor(rope, 4, kPairs), qsizetype(3)); // after ")"
    QCOMPARE(bracketNearCursor(rope, 0, kPairs), qsizetype(-1));
    QCOMPARE(bracketNearCursor(rope, 5, kPairs), qsizetype(-1));
    // Between ")(" the one after the cursor wins.
    const Rope two = Rope::fromString(u")("_s);
    QCOMPARE(bracketNearCursor(two, 1, kPairs), qsizetype(1));
  }
};

QTEST_APPLESS_MAIN(TstBracketMatch)
#include "tst_bracketmatch.moc"
