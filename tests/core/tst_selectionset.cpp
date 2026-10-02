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
};

QTEST_APPLESS_MAIN(TstSelectionSet)
#include "tst_selectionset.moc"
