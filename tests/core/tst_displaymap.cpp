#include "core/displaymap.h"

#include <QtTest>

using namespace qce;
using namespace Qt::StringLiterals;

class TstDisplayMap : public QObject {
  Q_OBJECT
private slots:
  void identityMapping() {
    TextDocument doc;
    doc.setText(u"one\ntwo\r\nthree\n"_s);
    DisplayMap map(&doc);
    QCOMPARE(map.rowCount(), 4);
    for (qsizetype i = 0; i < 4; ++i) {
      QCOMPARE(map.lineForRow(i), i);
      QCOMPARE(map.firstRowOfLine(i), i);
      QCOMPARE(map.rowCountOfLine(i), 1);
    }
    const DisplayRow row = map.rowAt(1);
    QCOMPARE(row.line, 1);
    QCOMPARE(row.startColumn, 0);
    QCOMPARE(row.endColumn, 3);      // the CRLF is not content
    QCOMPARE(map.lineForRow(99), 3); // clamped
    QCOMPARE(map.lineForRow(-5), 0);
    QCOMPARE(map.rowForPosition({2, 4}), 2);
  }

  void identityRowsAreWholeLines() {
    TextDocument doc;
    doc.setText(u"one\ntwo"_s);
    DisplayMap map(&doc);
    const DisplayRow row = map.rowAt(0);
    QVERIFY(row.isFirst());
    QVERIFY(row.isLast());
    QCOMPARE(row.rowInLine, 0);
    QCOMPARE(row.rowsInLine, 1);
    QCOMPARE(row.indent, 0.0);
    QCOMPARE(row.lastCursorColumn(), 3);
    // A row that continues on the next one keeps the cursor off the break.
    DisplayRow first{0, 0, 5, 0, 2, 0};
    QCOMPARE(first.lastCursorColumn(), 4);
  }

  void foldMapIsIdentity() {
    TextDocument doc;
    doc.setText(u"a\nb\nc"_s);
    FoldMap fold(&doc);
    QCOMPARE(fold.lineCount(), 3);
    QCOMPARE(fold.foldLineForBufferLine(2), 2);
    QCOMPARE(fold.bufferLineForFoldLine(9), 2);
    QVERIFY(fold.isVisible(1));
  }

  void reportsRowChanges() {
    TextDocument doc;
    doc.setText(u"a\nb\nc"_s);
    DisplayMap map(&doc);
    QSignalSpy spy(&map, &DisplayMap::rowsChanged);
    doc.insert(1, u"x"_s); // within line 0
    QCOMPARE(spy.takeLast(), (QList<QVariant>{0, 1, 1}));
    doc.insert(doc.length(), u"\nd\ne"_s); // appends two lines
    QCOMPARE(spy.takeLast(), (QList<QVariant>{2, 1, 3}));
    QCOMPARE(map.rowCount(), 5);
    doc.remove(0, doc.length());
    QCOMPARE(spy.takeLast(), (QList<QVariant>{0, 5, 1}));
    QCOMPARE(map.rowCount(), 1);
  }

  void resetSignal() {
    TextDocument doc;
    DisplayMap map(&doc);
    QSignalSpy spy(&map, &DisplayMap::reset);
    doc.setText(u"x\ny"_s);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(map.rowCount(), 2);
  }
};

QTEST_APPLESS_MAIN(TstDisplayMap)
#include "tst_displaymap.moc"
