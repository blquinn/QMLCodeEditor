#include "core/rope.h"
#include "testutil.h"

#include <QtTest>

using namespace qce;

class TstRope : public QObject {
  Q_OBJECT
private slots:
  void empty() {
    Rope r;
    QCOMPARE(r.length(), 0);
    QVERIFY(r.isEmpty());
    QCOMPARE(r.toString(), QString());
    QVERIFY(r.validate());
    QCOMPARE(Rope::fromString(u"").length(), 0);
    QCOMPARE(r.insert(0, u"hi").toString(), QStringLiteral("hi"));
    QVERIFY(r.insert(0, u"hi").remove(0, 2).isEmpty());
  }

  void basicEdits() {
    Rope r = Rope::fromString(u"hello world");
    QCOMPARE(r.at(4), QLatin1Char('o'));
    Rope r2 = r.insert(5, u",").replace(7, 12, u"there");
    QCOMPARE(r2.toString(), QStringLiteral("hello, there"));
    QCOMPARE(r.toString(), QStringLiteral("hello world")); // persistent
    QCOMPARE(r2.slice(1, 4).toString(), QStringLiteral("ell"));
    QCOMPARE(r2.newlineCount(), 0);
    QCOMPARE(r.insert(-5, u"<").insert(99, u">").toString(), QStringLiteral("<hello world>"));
  }

  void builderAndHeight() {
    test::Random rnd(7);
    const QString big = rnd.text(1'500'000);
    Rope r = Rope::fromString(big);
    QVERIFY2(r.validate(), "bulk build");
    QCOMPARE(r.length(), big.size());
    QCOMPARE(r.newlineCount(), big.count(QLatin1Char('\n')));
    QVERIFY(r.stats().height >= 2);
    QCOMPARE(r.toString(), big);

    RopeBuilder b;
    for (qsizetype i = 0; i < big.size(); i += 777)
      b.append(QStringView(big).mid(i, 777));
    QCOMPARE(b.snapshot().length(), big.size());
    Rope r2 = b.finish();
    QVERIFY(r2.validate());
    QCOMPARE(r2.toString(), big);
  }

  void chunksNeverSplitPairs_data() {
    QTest::addColumn<int>("pairAt");
    // Leaves from the builder are 1024 units, so 1023 puts the pair across a leaf boundary.
    QTest::newRow("straddle") << 1023;
    QTest::newRow("inside") << 500;
    QTest::newRow("second boundary") << 2047;
  }

  void chunksNeverSplitPairs() {
    QFETCH(int, pairAt);
    QString text = QString(4000, QLatin1Char('x'));
    text.replace(pairAt, 2, QStringLiteral("\U0001F600"));
    const Rope r = Rope::fromString(text);
    for (qsizetype from : {qsizetype(0), qsizetype(1), qsizetype(pairAt), qsizetype(pairAt + 2)}) {
      ChunkIterator it(r, from);
      QStringView chunk;
      QString got;
      while (it.next(&chunk)) {
        QVERIFY(!chunk.isEmpty());
        QVERIFY2(!chunk.back().isHighSurrogate(), "chunk ends in a high surrogate");
        QVERIFY2(!chunk.front().isLowSurrogate(), "chunk starts with a low surrogate");
        got += chunk;
      }
      QCOMPARE(got, text.mid(from));
    }
  }

  void sliceConcatRoundTrip() {
    test::Random rnd(3);
    const QString text = rnd.text(200'000);
    const Rope r = Rope::fromString(text);
    for (int i = 0; i < 200; ++i) {
      const qsizetype a = rnd.below(int(text.size()));
      const qsizetype b = a + rnd.below(int(text.size() - a));
      const Rope s = r.slice(a, b);
      QVERIFY(s.validate());
      QCOMPARE(s.toString(), text.mid(a, b - a));
      const Rope joined = r.slice(0, a).concat(r.slice(a, text.size()));
      QVERIFY(joined.validate());
      QCOMPARE(joined.length(), text.size());
      if (i % 20 == 0)
        QCOMPARE(joined.toString(), text);
    }
  }

  void snapshotsAreUnaffectedByEdits() {
    test::Random rnd(5);
    const QString text = rnd.text(100'000);
    const Rope before = Rope::fromString(text);
    Rope after = before;
    for (int i = 0; i < 200; ++i)
      after = after.insert(rnd.below(int(after.length())), rnd.text(rnd.range(1, 50)));
    QCOMPARE(before.toString(), text);
    QVERIFY(!before.sharesRootWith(after));
    QVERIFY(before.sharesRootWith(Rope(before)));
  }

  void randomEditsMatchModel() {
    const quint32 seed = test::testSeed();
    const int iterations = test::testIterations(3000);
    qInfo() << "seed" << seed << "iterations" << iterations;
    test::Random rnd(seed);

    QString model = rnd.text(rnd.range(0, 50'000));
    Rope rope = Rope::fromString(model);
    for (int i = 0; i < iterations; ++i) {
      const int len = int(model.size());
      const int op = rnd.below(100);
      if (op < 35) {
        const int at = rnd.below(len + 1);
        const QString t = rnd.text(rnd.range(1, 40));
        model.insert(at, t);
        rope = rope.insert(at, t);
      } else if (op < 60) {
        const int a = rnd.below(len + 1);
        const int b = qMin(len, a + rnd.range(0, 40));
        model.remove(a, b - a);
        rope = rope.remove(a, b);
      } else if (op < 75) {
        const int a = rnd.below(len + 1);
        const int b = qMin(len, a + rnd.range(0, 3000));
        const QString t = rnd.text(rnd.range(0, 3000));
        model.replace(a, b - a, t);
        rope = rope.replace(a, b, t);
      } else if (op < 82) {
        const int at = rnd.below(len + 1);
        const QString t = rnd.text(rnd.range(2000, 30000));
        model.insert(at, t);
        rope = rope.insert(at, t);
      } else if (op < 88 && len > 0) {
        const int a = rnd.below(len);
        const int b = qMin(len, a + rnd.range(1000, 40000));
        model.remove(a, b - a);
        rope = rope.remove(a, b);
      } else if (op < 94 && len > 0) {
        const int a = rnd.below(len + 1);
        const int b = a + rnd.below(len - a + 1);
        QCOMPARE(rope.slice(a, b).toString(), model.mid(a, b - a));
      } else if (len > 0) {
        const int a = rnd.below(len);
        QCOMPARE(rope.at(a), model.at(a));
      }
      if (rope.length() != model.size() || rope.newlineCount() != model.count(QLatin1Char('\n')))
        QFAIL(qPrintable(QStringLiteral("summary mismatch at iteration %1").arg(i)));
      if (i % 25 == 0) {
        QString why;
        if (!rope.validate(&why))
          QFAIL(qPrintable(QStringLiteral("invalid tree at %1: %2").arg(i).arg(why)));
        QVERIFY(rope.toString() == model);
      }
    }
    QVERIFY(rope.validate());
    QVERIFY(rope.toString() == model);
    const Rope::Stats s = rope.stats();
    qInfo() << "height" << s.height << "leaves" << s.leaves << "underfull" << s.underfullLeaves;
    // Fragmentation stays bounded: few leaves are underfull.
    QVERIFY(s.underfullLeaves <= s.leaves / 4 + 4);
  }
};

QTEST_APPLESS_MAIN(TstRope)
#include "tst_rope.moc"
