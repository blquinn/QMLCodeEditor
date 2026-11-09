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

  // The reference the sweeps must agree with: every blank line searches outward for itself.
  static QList<int> bruteForce(const Rope &rope, qsizetype first, qsizetype last, int tabWidth, int unit, qsizetype maxSearch) {
    const qsizetype lineCount = rope.lineCount();
    auto own = [&](qsizetype l) {
      return indentColumns(QStringView(rope.toString(rope.lineStart(l), rope.lineEnd(l))), tabWidth);
    };
    QList<int> out;
    for (qsizetype line = first; line <= last; ++line) {
      if (const int indent = own(line); indent >= 0) {
        out.append(indent);
        continue;
      }
      int a = 0, b = 0;
      for (qsizetype l = line - 1, n = 0; l >= 0 && n < maxSearch; --l, ++n)
        if (const int i = own(l); i >= 0) {
          a = i;
          break;
        }
      for (qsizetype l = line + 1, n = 0; l < lineCount && n < maxSearch; ++l, ++n)
        if (const int i = own(l); i >= 0) {
          b = i;
          break;
        }
      out.append(a == b ? a : qMin(qMin(a, b) + unit, qMax(a, b)));
    }
    return out;
  }

  void sweepsAgreeWithPerLineSearch() {
    // Text and blank lines in irregular runs, with the viewport and the search limit varied against them.
    QString text;
    quint32 seed = 12345;
    auto next = [&] { return seed = seed * 1664525u + 1013904223u; };
    for (int l = 0; l < 120; ++l) {
      const quint32 r = next() >> 16;
      if (r % 5 < 3)
        text += QString(qsizetype(r % 4) * 4, u' ') + u"x"_s;
      else if (r % 5 == 3)
        text += QString(qsizetype(r % 3), u' '); // whitespace only: blank too
      text += u'\n';
    }
    const Rope rope = Rope::fromString(text);
    for (qsizetype maxSearch : {qsizetype(1), qsizetype(2), qsizetype(3), qsizetype(7), qsizetype(1000)})
      for (qsizetype first : {0, 1, 17, 60, 100})
        for (qsizetype len : {0, 1, 9, 30}) {
          const qsizetype last = qMin<qsizetype>(first + len, rope.lineCount() - 1);
          QCOMPARE(effectiveIndents(rope, first, last, 4, 4, maxSearch), bruteForce(rope, first, last, 4, 4, maxSearch));
        }
  }

  void aLongBlankStretchResolvesFromItsEnds() {
    // A viewport inside a long run of blank lines (the blank side of a diff): every line takes its answer from the
    // text lines at either end of the run, however far they are, up to the search limit.
    const QString text = u"    a\n"_s + QString(1500, u'\n') + u"        b"_s;
    const Rope rope = Rope::fromString(text);
    // Both ends are within reach: the smaller indent plus a unit, up to the larger.
    QCOMPARE(effectiveIndents(rope, 700, 719, 4, 4), QList<int>(20, 8));
    QCOMPARE(effectiveIndents(rope, 700, 719, 4, 2), QList<int>(20, 6));
    // Out of reach of both ends: nothing to inherit.
    QCOMPARE(effectiveIndents(rope, 700, 719, 4, 4, 100), QList<int>(20, 0));
    QCOMPARE(effectiveIndents(rope, 700, 719, 4, 4, 1000), bruteForce(rope, 700, 719, 4, 4, 1000));
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
