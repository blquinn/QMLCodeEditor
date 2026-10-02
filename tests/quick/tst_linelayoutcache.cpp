#include "quick/linelayoutcache.h"

#include <QtTest>

using qce::LineLayoutCache;

class TstLineLayoutCache : public QObject {
  Q_OBJECT

  static std::shared_ptr<qce::LineLayout> add(LineLayoutCache &cache, qsizetype line) {
    return cache.insert(line, std::make_unique<QTextLayout>(), 1.0);
  }

private slots:
  void hitsAndMisses() {
    LineLayoutCache cache(4);
    QVERIFY(!cache.find(1));
    const auto a = add(cache, 1);
    QCOMPARE(cache.find(1), a);
    QCOMPARE(cache.stats().hits, 1u);
    QCOMPARE(cache.stats().created, 1u);
    QVERIFY(a->id != 0);
    QVERIFY(add(cache, 2)->id != a->id);
  }

  void rowsOfALineAreSeparateEntries() {
    LineLayoutCache cache(8);
    const auto first = cache.insert(5, std::make_unique<QTextLayout>(), 1.0, {}, 0);
    const auto second = cache.insert(5, std::make_unique<QTextLayout>(), 1.0, {}, 1);
    QCOMPARE(cache.find(5, 0), first);
    QCOMPARE(cache.find(5, 1), second);
    QVERIFY(!cache.find(5, 2));
    // Replacing a line drops all its rows and renumbers later ones.
    const auto later = cache.insert(9, std::make_unique<QTextLayout>(), 1.0, {}, 3);
    cache.invalidate(5, 1, 2);
    QVERIFY(!cache.find(5, 0));
    QVERIFY(!cache.find(5, 1));
    QCOMPARE(cache.find(10, 3), later);
  }

  void evictsLeastRecentlyUsed() {
    LineLayoutCache cache(3);
    add(cache, 1);
    add(cache, 2);
    add(cache, 3);
    QVERIFY(cache.find(1)); // 2 is now the oldest
    add(cache, 4);
    QCOMPARE(cache.size(), 3);
    QVERIFY(!cache.find(2));
    QVERIFY(cache.find(1) && cache.find(3) && cache.find(4));
    QCOMPARE(cache.stats().evicted, 1u);
    cache.setCapacity(1);
    QCOMPARE(cache.size(), 1);
  }

  void evictedLayoutStaysAliveForHolders() {
    LineLayoutCache cache(1);
    const auto a = add(cache, 1);
    add(cache, 2);
    QVERIFY(!cache.find(1));
    QVERIFY(a->layout); // a frame plan can still draw from it
  }

  void invalidateShiftsLaterLines() {
    LineLayoutCache cache(16);
    for (qsizetype i = 0; i < 10; ++i)
      add(cache, i);
    const auto before7 = cache.find(7);
    // Lines 3..4 replaced by three lines: 3..5 are new, old 5.. move down by one.
    cache.invalidate(3, 2, 3);
    QVERIFY(!cache.find(3) && !cache.find(4));
    QVERIFY(cache.find(2));
    QCOMPARE(cache.find(8), before7);
    QVERIFY(!cache.find(5)); // old line 5 is now line 6; line 5 is new and empty
    QVERIFY(cache.find(6));
    // Deleting lines pulls later lines up.
    cache.invalidate(0, 3, 1);
    QCOMPARE(cache.find(6), before7);
  }

  void singleLineEditDropsOnlyThatLine() {
    LineLayoutCache cache(16);
    for (qsizetype i = 0; i < 5; ++i)
      add(cache, i);
    cache.invalidate(2, 1, 1);
    QVERIFY(!cache.find(2));
    QCOMPARE(cache.size(), 4);
  }

  void clearFrom() {
    LineLayoutCache cache(16);
    for (qsizetype i = 0; i < 6; ++i)
      add(cache, i);
    cache.clear(4);
    QCOMPARE(cache.size(), 4);
    QVERIFY(!cache.find(4) && cache.find(3));
    cache.clear();
    QCOMPARE(cache.size(), 0);
  }
};

QTEST_MAIN(TstLineLayoutCache)
#include "tst_linelayoutcache.moc"
