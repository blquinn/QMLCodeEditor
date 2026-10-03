#include "core/anchorset.h"
#include "testutil.h"

#include <QtTest>

#include <QElapsedTimer>

#include <random>

using namespace qce;

namespace {

struct NaiveAnchor {
  AnchorId id;
  qsizetype offset;
  Gravity gravity;
};

// The specification of an edit's effect on one anchor, written independently of AnchorSet.
qsizetype mapOffset(qsizetype p, Gravity g, qsizetype start, qsizetype oldEnd, qsizetype newEnd) {
  if (p < start)
    return p;
  if (p > oldEnd)
    return p + (newEnd - oldEnd);
  if (p == oldEnd && oldEnd > start)
    return newEnd;
  return g == Gravity::Left ? start : newEnd;
}

} // namespace

class TstAnchors : public QObject {
  Q_OBJECT
private slots:
  void gravityAtInsertPoint() {
    AnchorSet set;
    const AnchorId l = set.create(5, Gravity::Left);
    const AnchorId r = set.create(5, Gravity::Right);
    set.applyEdit(5, 5, 8); // insert 3 units at 5
    QCOMPARE(set.offset(l), 5);
    QCOMPARE(set.offset(r), 8);
    set.applyEdit(0, 0, 2);
    QCOMPARE(set.offset(l), 7);
    QCOMPARE(set.offset(r), 10);
  }

  void deletionCollapses() {
    AnchorSet set;
    const AnchorId before = set.create(2);
    const AnchorId atStart = set.create(4, Gravity::Left);
    const AnchorId inside = set.create(6, Gravity::Left);
    const AnchorId insideR = set.create(6, Gravity::Right);
    const AnchorId atEnd = set.create(8, Gravity::Left);
    const AnchorId after = set.create(12);
    set.applyEdit(4, 8, 4); // delete [4, 8)
    QCOMPARE(set.offset(before), 2);
    QCOMPARE(set.offset(atStart), 4);
    QCOMPARE(set.offset(inside), 4);
    QCOMPARE(set.offset(insideR), 4);
    QCOMPARE(set.offset(atEnd), 4);
    QCOMPARE(set.offset(after), 8);
    QVERIFY(set.validate());
  }

  void replacementKeepsEndAnchorsAfterNewText() {
    AnchorSet set;
    const AnchorId atEnd = set.create(8, Gravity::Left);
    const AnchorId inside = set.create(6, Gravity::Right);
    const AnchorId insideL = set.create(6, Gravity::Left);
    set.applyEdit(4, 8, 7); // replace [4, 8) with 3 units
    QCOMPARE(set.offset(atEnd), 7);
    QCOMPARE(set.offset(inside), 7);
    QCOMPARE(set.offset(insideL), 4);
  }

  void removeAndReuseIds() {
    AnchorSet set;
    const AnchorId a = set.create(1);
    const AnchorId b = set.create(2);
    set.remove(a);
    QVERIFY(!set.contains(a));
    QVERIFY(set.contains(b));
    QCOMPARE(set.size(), 1);
    const AnchorId c = set.create(3);
    QVERIFY(set.contains(c));
    QCOMPARE(set.offset(b), 2);
    QCOMPARE(set.offset(c), 3);
    QVERIFY(set.validate());
  }

  void randomEditsMatchNaiveModel() {
    test::Random rnd(test::testSeed());
    for (int round = 0; round < 4; ++round) {
      AnchorSet set;
      QList<NaiveAnchor> model;
      qsizetype docLen = 5000;
      // few anchors -> single block; many -> many blocks (blocks hold at most 512)
      const int initial = round < 2 ? 50 : 4000;
      for (int i = 0; i < initial; ++i) {
        const Gravity g = rnd.chance(50) ? Gravity::Left : Gravity::Right;
        const qsizetype off = rnd.below(int(docLen) + 1);
        model.append({set.create(off, g), off, g});
      }
      for (int i = 0; i < test::testIterations(1500); ++i) {
        const int op = rnd.below(100);
        if (op < 10) {
          const Gravity g = rnd.chance(50) ? Gravity::Left : Gravity::Right;
          const qsizetype off = rnd.below(int(docLen) + 1);
          model.append({set.create(off, g), off, g});
        } else if (op < 18 && !model.isEmpty()) {
          const int k = rnd.below(int(model.size()));
          set.remove(model[k].id);
          model.removeAt(k);
        } else {
          const bool big = rnd.chance(5);
          const qsizetype start = rnd.below(int(docLen) + 1);
          const qsizetype oldEnd = qMin(docLen, start + rnd.range(0, big ? 3000 : 12));
          const qsizetype newLen = rnd.range(0, big ? 3000 : 12);
          const qsizetype newEnd = start + newLen;
          set.applyEdit(start, oldEnd, newEnd);
          for (NaiveAnchor &a : model)
            a.offset = mapOffset(a.offset, a.gravity, start, oldEnd, newEnd);
          docLen += newEnd - oldEnd;
        }
        QVERIFY2(set.validate(), "invariants broken");
        QCOMPARE(set.size(), model.size());
        if (i % 10 == 0)
          for (const NaiveAnchor &a : model)
            if (set.offset(a.id) != a.offset)
              QFAIL(qPrintable(QStringLiteral("round %1 step %2: anchor %3 at %4, expected %5")
                                 .arg(round)
                                 .arg(i)
                                 .arg(a.id)
                                 .arg(set.offset(a.id))
                                 .arg(a.offset)));
      }
    }
  }

  void handlesHundredThousandAnchors() {
    test::Random rnd(9);
    AnchorSet set;
    constexpr int N = 100'000;
    QList<AnchorId> ids;
    ids.reserve(N);
    for (int i = 0; i < N; ++i)
      ids << set.create(i * 10, i % 2 ? Gravity::Left : Gravity::Right);
    QVERIFY(set.validate());

    QElapsedTimer t;
    t.start();
    constexpr int edits = 2000;
    for (int i = 0; i < edits; ++i) {
      const qsizetype at = rnd.below(N * 10);
      set.applyEdit(at, at, at + 3);
    }
    const qint64 ms = t.elapsed();
    qInfo() << edits << "edits with" << N << "anchors:" << ms << "ms";
    QVERIFY(set.validate());
    QCOMPARE(set.size(), N);
    // anchors only ever move right here and stay ordered
    qsizetype prev = -1;
    for (AnchorId id : ids) {
      QVERIFY(set.offset(id) >= prev);
      prev = set.offset(id);
    }
  }

  void moveKeepsIdAndOrder() {
    AnchorSet set;
    std::vector<AnchorId> ids;
    std::vector<qsizetype> expected;
    std::mt19937_64 rng(7);
    for (int i = 0; i < 3000; ++i) {
      expected.push_back(qsizetype(rng() % 10000));
      ids.push_back(set.create(expected.back(), i % 2 ? Gravity::Left : Gravity::Right));
    }
    for (int round = 0; round < 20000; ++round) {
      const size_t k = size_t(rng() % ids.size());
      expected[k] = qsizetype(rng() % 12000);
      set.move(ids[k], expected[k]);
      if (round % 997 == 0)
        QVERIFY(set.validate());
    }
    QVERIFY(set.validate());
    for (size_t k = 0; k < ids.size(); ++k)
      QCOMPARE(set.offset(ids[k]), expected[k]);
    QCOMPARE(set.size(), qsizetype(ids.size()));
    // Gravity is kept, and moved anchors still follow edits.
    set.applyEdit(0, 0, 5);
    for (size_t k = 0; k < ids.size(); ++k)
      QCOMPARE(set.offset(ids[k]), expected[k] + (expected[k] == 0 ? (k % 2 ? 0 : 5) : 5));
    QVERIFY(set.validate());
  }
};

QTEST_APPLESS_MAIN(TstAnchors)
#include "tst_anchors.moc"
