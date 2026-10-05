#include "core/indentation.h"
#include "testutil.h"

#include <QtTest>

using namespace qce;
using namespace Qt::StringLiterals;

namespace {

Rope rope(const QString &text) { return Rope().insert(0, text); }

QString nested(int width, const QString &unit = {}) {
  const QString pad = unit.isEmpty() ? QString(width, u' ') : unit;
  return u"a {\n"_s + pad + u"b {\n"_s + pad + pad + u"c;\n"_s + pad + u"}\n}\n"_s;
}

} // namespace

class TstIndentation : public QObject {
  Q_OBJECT
private slots:
  void spaceWidths_data() {
    QTest::addColumn<int>("width");
    for (int w : {2, 3, 4, 8})
      QTest::newRow(qPrintable(QString::number(w))) << w;
  }
  void spaceWidths() {
    QFETCH(int, width);
    const auto guess = detectIndentation(rope(nested(width)));
    QVERIFY(guess);
    QVERIFY(guess->insertSpaces);
    QCOMPARE(guess->indentWidth, width);
  }

  void tabs() {
    const auto guess = detectIndentation(rope(nested(0, u"\t"_s)));
    QVERIFY(guess);
    QVERIFY(!guess->insertSpaces);
  }

  void majorityWins() {
    const auto guess = detectIndentation(rope(nested(2) + nested(2) + nested(0, u"\t"_s)));
    QVERIFY(guess);
    QVERIFY(guess->insertSpaces);
    QCOMPARE(guess->indentWidth, 2);
  }

  void blockCommentsAreNotWidthOne() {
    const auto guess = detectIndentation(rope(u"/**\n * doc\n * more\n */\nint f() {\n    return 1;\n}\n"_s));
    QVERIFY(guess);
    QCOMPARE(guess->indentWidth, 4);
  }

  void unindentedTextGivesNothing() { QVERIFY(!detectIndentation(rope(u"a\nb\n\nc"_s))); }

  void crlfAndBlankLines() {
    QString text = nested(2);
    text.replace(u'\n', u"\r\n"_s);
    text += u"\r\n   \r\n"_s;
    const auto guess = detectIndentation(rope(text));
    QVERIFY(guess);
    QCOMPARE(guess->indentWidth, 2);
  }

  void maxLinesIsHonored() {
    const QString text = u"a\nb\n"_s + nested(4);
    QVERIFY(!detectIndentation(rope(text), 2));
    QVERIFY(detectIndentation(rope(text), 100));
  }

  void largeDocument() {
    QString block;
    for (int i = 0; i < 2000; ++i)
      block += nested(4);
    Rope big = rope(block);
    std::optional<IndentationGuess> guess;
    QBENCHMARK { guess = detectIndentation(big); }
    QVERIFY(guess);
    QCOMPARE(guess->indentWidth, 4);
  }
};

QTEST_APPLESS_MAIN(TstIndentation)
#include "tst_indentation.moc"
