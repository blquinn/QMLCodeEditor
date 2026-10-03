#include "core/selectionset.h"
#include "core/textdocument.h"

#include <QtTest>

using namespace qce;
using namespace Qt::StringLiterals;

class TstSelectionSet : public QObject {
  Q_OBJECT
private slots:
  void startsAsOneCursor() {
    TextDocument doc;
    SelectionSet set(&doc);
    QCOMPARE(set.count(), 1);
    QCOMPARE(set.primary(), (Selection{0, 0}));
  }

  void clampsAndAvoidsSurrogateInteriors() {
    TextDocument doc;
    doc.setText(u"a\U0001F600b");
    SelectionSet set(&doc);
    set.setSingle(2, 999);
    QCOMPARE(set.primary(), (Selection{1, 4}));
  }

  void sortsAndMergesOverlapAndTouching() {
    TextDocument doc;
    doc.setText(u"0123456789");
    SelectionSet set(&doc);
    set.set({{8, 9}, {1, 3}, {3, 5}, {4, 6}}, 3);
    QCOMPARE(set.selections(), (SelectionList{{1, 6}, {8, 9}}));
    QCOMPARE(set.primaryIndex(), 0); // the primary was merged into the first
    set.set({{6, 2}, {1, 3}});
    QCOMPARE(set.selections(), (SelectionList{{6, 1}})); // the larger one decides the direction
    set.set({});
    QCOMPARE(set.selections(), (SelectionList{{0, 0}}));
  }

  void followEditsAndSignalOnce() {
    TextDocument doc;
    doc.setText(u"0123456789");
    SelectionSet set(&doc);
    set.set({{2, 4}, {7, 7}});
    QSignalSpy spy(&set, &SelectionSet::changed);
    doc.insert(0, u"ZZ");
    QCOMPARE(set.selections(), (SelectionList{{4, 6}, {9, 9}}));
    QCOMPARE(spy.count(), 1);
    {
      SelectionSet::Batch batch(set);
      doc.insert(0, u"a");
      doc.insert(0, u"b");
      QCOMPARE(spy.count(), 1);
    }
    QCOMPARE(spy.count(), 2);
  }

  void resetAndLoadingKeepCursorAtStart() {
    TextDocument doc;
    SelectionSet set(&doc);
    doc.setText(u"hello");
    QCOMPARE(set.primary(), (Selection{0, 0}));
    set.setSingle(3);
    doc.setText(u"other");
    QCOMPARE(set.primary(), (Selection{0, 0}));
  }

  void destroyingReleasesAnchors() {
    TextDocument doc;
    {
      SelectionSet set(&doc);
      set.set({{0, 0}, {0, 0}});
    }
    QCOMPARE(doc.anchors().size(), 0);
  }

  void reusesAnchorsWhenSizeIsUnchanged() {
    TextDocument doc;
    doc.setText(QString(100, u'x'));
    SelectionSet set(&doc);
    set.set({{1, 1}, {5, 7}, {20, 20}});
    const qsizetype anchors = doc.anchors().size();
    set.set({{2, 2}, {6, 9}, {30, 29}}, 2);
    QCOMPARE(doc.anchors().size(), anchors);
    QCOMPARE(set.selections(), (SelectionList{{2, 2}, {6, 9}, {30, 29}}));
    QCOMPARE(set.primaryIndex(), 2);
    set.set({{2, 2}});
    QCOMPARE(doc.anchors().size(), anchors - 4);
    QVERIFY(doc.anchors().validate());
  }

  void indexAtAndLowerBound() {
    TextDocument doc;
    doc.setText(QString(100, u'x'));
    SelectionSet set(&doc);
    set.set({{2, 2}, {10, 20}, {50, 50}});
    QCOMPARE(set.indexAt(2), 0);
    QCOMPARE(set.indexAt(3), -1);
    QCOMPARE(set.indexAt(10), 1);
    QCOMPARE(set.indexAt(15), 1);
    QCOMPARE(set.indexAt(20), 1);
    QCOMPARE(set.indexAt(50), 2);
    QCOMPARE(set.indexAt(51), -1);
    QCOMPARE(set.lowerBound(0), 0);
    QCOMPARE(set.lowerBound(3), 1);
    QCOMPARE(set.lowerBound(21), 2);
    QCOMPARE(set.lowerBound(99), 3);
  }

  void addAndPrimary() {
    TextDocument doc;
    doc.setText(QString(100, u'x'));
    SelectionSet set(&doc);
    set.set({{10, 10}, {40, 40}});
    set.add({20, 25});
    QCOMPARE(set.selections(), (SelectionList{{10, 10}, {20, 25}, {40, 40}}));
    QCOMPARE(set.primaryIndex(), 1);
    set.add({24, 30}); // overlaps: merged
    QCOMPARE(set.selections(), (SelectionList{{10, 10}, {20, 30}, {40, 40}}));
    QCOMPARE(set.primaryIndex(), 1);
    set.add({0, 0});
    QCOMPARE(set.primaryIndex(), 0);
    set.setPrimary(2);
    QCOMPARE(set.primary(), (Selection{20, 30}));
    set.collapseToPrimary();
    QCOMPARE(set.selections(), (SelectionList{{20, 30}}));
    QVERIFY(doc.anchors().validate());
  }

  void externalDeleteMergesSelections() {
    TextDocument doc;
    doc.setText(u"0123456789");
    SelectionSet set(&doc);
    set.set({{1, 1}, {3, 3}, {5, 6}, {9, 9}}, 2);
    int emitted = 0;
    connect(&set, &SelectionSet::changed, this, [&] { ++emitted; });
    doc.remove(2, 7); // swallows the middle two
    QCOMPARE(set.selections(), (SelectionList{{1, 1}, {2, 2}, {4, 4}}));
    QCOMPARE(set.primaryIndex(), 1);
    QCOMPARE(emitted, 1);
    doc.remove(1, 2);
    QCOMPARE(set.selections(), (SelectionList{{1, 1}, {3, 3}}));
  }
};

QTEST_APPLESS_MAIN(TstSelectionSet)
#include "tst_selectionset.moc"
