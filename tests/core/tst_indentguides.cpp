#include "core/indentguides.h"
#include "core/rope.h"

#include <QtTest>

using namespace qce;
using namespace Qt::StringLiterals;

namespace {
QList<int> indents(const QString &text, qsizetype first, qsizetype last, int unit = 4) {
  return effectiveIndents(Rope::fromString(text), first, last, 4, unit);
}
} // namespace

class TstIndentGuides : public QObject {
  Q_OBJECT
private slots:
  void indentColumnsCountsCells() {
    QCOMPARE(indentColumns(u"abc", 4), 0);
    QCOMPARE(indentColumns(u"    abc", 4), 4);
    QCOMPARE(indentColumns(u"\tabc", 4), 4);
    QCOMPARE(indentColumns(u"  \tabc", 4), 4);
    QCOMPARE(indentColumns(u"\t  abc", 4), 6);
    QCOMPARE(indentColumns(u"\tabc", 8), 8);
    QCOMPARE(indentColumns(u"", 4), -1);
    QCOMPARE(indentColumns(u"  \t ", 4), -1);
  }

  void textLinesKeepTheirOwnIndent() {
    QCOMPARE(indents(u"a\n    b\n  c"_s, 0, 2), (QList<int>{0, 4, 2}));
  }

  void blankLineInsideABlock() {
    QCOMPARE(indents(u"if {\n    a\n\n    b\n}"_s, 0, 4), (QList<int>{0, 4, 4, 4, 0}));
  }

  void blankLineAtTheEndOfABlock() {
    // Between a deeper line above and a shallower one below: one unit more than the shallower.
    QCOMPARE(indents(u"f {\n    a\n\n}"_s, 0, 3), (QList<int>{0, 4, 4, 0}));
    QCOMPARE(indents(u"f {\n        a\n\n}"_s, 0, 3), (QList<int>{0, 8, 4, 0}));
  }

  void blankLineAtTheStartOfABlock() {
    QCOMPARE(indents(u"f {\n\n        a"_s, 0, 2), (QList<int>{0, 4, 8}));
  }

  void runsOfBlankLinesAreTreatedAlike() {
    QCOMPARE(indents(u"f {\n    a\n\n\n\n    b\n}"_s, 0, 6), (QList<int>{0, 4, 4, 4, 4, 4, 0}));
    QCOMPARE(indents(u"  \n\t\n    x"_s, 0, 2), (QList<int>{4, 4, 4}));
  }

  void searchLooksOutsideTheRange() {
    const QString text = u"f {\n    a\n\n\n    b\n}"_s;
    QCOMPARE(indents(text, 2, 3), (QList<int>{4, 4}));
    QCOMPARE(indents(text, 3, 3), (QList<int>{4}));
  }

  void documentEdgesCountAsZero() {
    QCOMPARE(indents(u"\n    a"_s, 0, 1), (QList<int>{4, 4}));
    QCOMPARE(indents(u"    a\n"_s, 0, 1), (QList<int>{4, 4}));
    QCOMPARE(indents(u"\n\n"_s, 0, 2), (QList<int>{0, 0, 0}));
  }

  void searchIsBounded() {
    QString text = u"        a\n"_s;
    text += QString(10, u'\n');
    text += u"        b"_s;
    const QList<int> far = effectiveIndents(Rope::fromString(text), 5, 5, 4, 4, 3);
    QCOMPARE(far, (QList<int>{0}));
    QCOMPARE(effectiveIndents(Rope::fromString(text), 5, 5, 4, 4, 100), (QList<int>{8}));
  }

  void rangeIsClamped() {
    QCOMPARE(indents(u"a\n  b"_s, -3, 10), (QList<int>{0, 2}));
    QVERIFY(indents(u"a"_s, 2, 1).isEmpty());
  }

  void longLinesOnlyCostAPrefix() {
    const QString text = u"    "_s + QString(200000, u'x');
    QCOMPARE(indents(text, 0, 0), (QList<int>{4}));
    const QString blank = QString(1000, u' ');
    QCOMPARE(indents(blank, 0, 0), (QList<int>{0}));
  }
};

QTEST_APPLESS_MAIN(TstIndentGuides)
#include "tst_indentguides.moc"
