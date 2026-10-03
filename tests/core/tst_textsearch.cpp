#include "core/textsearch.h"
#include "testutil.h"

#include <QtTest>

using namespace qce;
using namespace Qt::StringLiterals;

namespace {

QList<Selection> naiveAll(const QString &text, const QString &needle, search::Options o) {
  QList<Selection> out;
  qsizetype from = 0;
  const auto cs = o.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
  while (true) {
    qsizetype i = text.indexOf(needle, from, cs);
    if (i < 0)
      break;
    const bool before = i > 0 && search::isWordChar(text[i - 1]);
    const bool after = i + needle.size() < text.size() && search::isWordChar(text[i + needle.size()]);
    if (o.wholeWord && (before || after)) {
      from = i + 1;
      continue;
    }
    out.append({i, i + needle.size()});
    from = i + needle.size();
  }
  return out;
}

} // namespace

class TstTextSearch : public QObject {
  Q_OBJECT
private slots:
  void findsSimpleMatches() {
    const Rope rope = Rope::fromString(u"foo bar foo\nbaz foo"_s);
    QCOMPARE(search::findNext(rope, u"foo"_s, 0), (std::optional<Selection>(Selection{0, 3})));
    QCOMPARE(search::findNext(rope, u"foo"_s, 1), (std::optional<Selection>(Selection{8, 11})));
    QCOMPARE(search::findNext(rope, u"foo"_s, 12), (std::optional<Selection>(Selection{16, 19})));
    QVERIFY(!search::findNext(rope, u"foo"_s, 17));
    QCOMPARE(search::findNext(rope, u"foo"_s, 17, {}, true), (std::optional<Selection>(Selection{0, 3})));
    QVERIFY(!search::findNext(rope, u"nope"_s, 0, {}, true));
    QVERIFY(!search::findNext(rope, QString(), 0));
  }

  void wrapFindsMatchThatStartsBeforeFrom() {
    const Rope rope = Rope::fromString(u"abcabc"_s);
    // from inside the only match before it: wrap finds the match at 0
    QCOMPARE(search::findNext(rope, u"abc"_s, 1, {}, true), (std::optional<Selection>(Selection{3, 6})));
    const Rope one = Rope::fromString(u"xxabcxx"_s);
    QCOMPARE(search::findNext(one, u"abc"_s, 3, {}, true), (std::optional<Selection>(Selection{2, 5})));
  }

  void wholeWordAndCase() {
    const Rope rope = Rope::fromString(u"cat concat Cat cat_ (cat)"_s);
    QCOMPARE(search::findAll(rope, u"cat"_s, 100, {true, true}), (QList<Selection>{{0, 3}, {21, 24}}));
    QCOMPARE(search::findAll(rope, u"cat"_s, 100, {false, true}), (QList<Selection>{{0, 3}, {11, 14}, {21, 24}}));
    QCOMPARE(search::findAll(rope, u"cat"_s, 100, {true, false}).size(), 4);
  }

  void nonOverlappingAndCap() {
    const Rope rope = Rope::fromString(u"aaaaa"_s);
    QCOMPARE(search::findAll(rope, u"aa"_s, 100), (QList<Selection>{{0, 2}, {2, 4}}));
    bool capped = false;
    const Rope many = Rope::fromString(QString(u"ab").repeated(50));
    QCOMPARE(search::findAll(many, u"ab"_s, 10, {}, &capped).size(), 10);
    QVERIFY(capped);
    search::findAll(many, u"ab"_s, 50, {}, &capped);
    QVERIFY(!capped);
  }

  void differentialAgainstQString() {
    qce::test::Random rng(qce::test::testSeed());
    for (int round = 0; round < qce::test::testIterations(40); ++round) {
      QString text;
      const int len = rng.range(1, 30000);
      for (int i = 0; i < len; ++i) {
        const int c = rng.below(40);
        text += c < 14 ? u'a' : c < 28 ? u'b' : c < 30 ? u' ' : c < 32 ? u'\n' : c < 34 ? u'A' : c < 36 ? u'_' : u'é';
      }
      const Rope rope = Rope::fromString(text);
      QString needle;
      for (int i = rng.range(1, 6); i > 0; --i)
        needle += rng.chance(70) ? QChar(u'a') : QChar(rng.chance(50) ? u'b' : u'A');
      for (bool cs : {true, false})
        for (bool ww : {false, true}) {
          const search::Options o{cs, ww};
          QCOMPARE(search::findAll(rope, needle, 1 << 20, o), naiveAll(text, needle, o));
        }
      const search::Options o;
      const auto all = naiveAll(text, needle, o);
      const auto allOverlapping = [&] {
        QList<qsizetype> v;
        for (qsizetype i = text.indexOf(needle); i >= 0; i = text.indexOf(needle, i + 1))
          v.append(i);
        return v;
      }();
      for (int k = 0; k < 20; ++k) {
        const qsizetype from = rng.range(0, int(text.size()));
        std::optional<Selection> want;
        for (qsizetype i : allOverlapping)
          if (i >= from) {
            want = Selection{i, i + needle.size()};
            break;
          }
        QCOMPARE(search::findNext(rope, needle, from), want);
        if (!want && !allOverlapping.isEmpty())
          QCOMPARE(
            search::findNext(rope, needle, from, o, true),
            (std::optional<Selection>(Selection{allOverlapping.first(), allOverlapping.first() + needle.size()}))
          );
      }
      Q_UNUSED(all);
    }
  }
};

QTEST_APPLESS_MAIN(TstTextSearch)
#include "tst_textsearch.moc"
