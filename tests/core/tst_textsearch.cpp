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

  void compileQuery() {
    using search::compileQuery;
    QVERIFY(!compileQuery({}, {}).valid());
    const search::Pattern plain = compileQuery(u"a.b"_s, {});
    QVERIFY(plain.valid());
    QCOMPARE(plain.literal, u"a.b"_s);
    QVERIFY(!plain.regex.match(u"axb"_s).hasMatch()); // the dot is escaped
    QVERIFY(plain.regex.match(u"A.B"_s).hasMatch());   // case-insensitive by default
    const search::Pattern cs = compileQuery(u"a"_s, {false, true, false});
    QVERIFY(cs.caseSensitive);
    QVERIFY(!cs.regex.match(u"A"_s).hasMatch());
    const search::Pattern word = compileQuery(u"foo"_s, {false, true, true});
    QVERIFY(word.wholeWord);
    QVERIFY(word.regex.match(u"a foo b"_s).hasMatch());
    QVERIFY(!word.regex.match(u"afoo"_s).hasMatch());
    QVERIFY(!word.regex.match(u"foo_"_s).hasMatch());
    const search::Pattern re = compileQuery(u"f(o+)"_s, {true, true, false});
    QVERIFY(re.valid());
    QVERIFY(re.literal.isEmpty());
    const search::Pattern bad = compileQuery(u"("_s, {true, true, false});
    QVERIFY(!bad.valid());
    QVERIFY(!bad.error.isEmpty());
  }

  void patternSearchPicksTheEngine() {
    const QString text = u"Foo foo\nfoobar FOO"_s;
    const Rope rope = Rope::fromString(text);
    for (const bool regex : {false, true}) {
      const search::Pattern p = search::compileQuery(regex ? u"foo\\b"_s : u"foo"_s, {regex, false, !regex});
      const QList<Selection> all = search::findAll(rope, p, 0, rope.length(), 100);
      QCOMPARE(all, (QList<Selection>{{0, 3}, {4, 7}, {15, 18}}));
      QCOMPARE(search::find(rope, p, 0), (std::optional<Selection>(Selection{4, 7})));
      QCOMPARE(search::find(rope, p, 0, true, true, true), (std::optional<Selection>(Selection{0, 3})));
      QCOMPARE(search::find(rope, p, 15, true, true), (std::optional<Selection>(Selection{0, 3}))); // wraps
      QVERIFY(!search::find(rope, p, 15, true, false));
      QCOMPARE(search::find(rope, p, 4, false), (std::optional<Selection>(Selection{0, 3})));
      bool capped = false;
      QCOMPARE(search::findAll(rope, p, 0, rope.length(), 2, &capped).size(), 2);
      QVERIFY(capped);
    }
    QVERIFY(!search::find(rope, search::Pattern(), 0));
  }

  void cancelStopsTheSearch() {
    const Rope rope = Rope::fromString(u"foo\n"_s.repeated(1000));
    std::atomic_bool cancel{true};
    for (const bool regex : {false, true}) {
      const search::Pattern p = search::compileQuery(u"foo"_s, {regex, true, false});
      QVERIFY(search::findAll(rope, p, 0, rope.length(), 5000, nullptr, &cancel).size() < 1000);
    }
  }

  void forEachLineMatchWalksLines() {
    const Rope rope = Rope::fromString(u"a a\nb\na a a"_s);
    const QRegularExpression re(u"a"_s);
    QList<QPair<qsizetype, qsizetype>> seen;
    search::forEachLineMatch(rope, re, 0, 2, [&](qsizetype base, const QRegularExpressionMatch &m) {
      seen.append({base, m.capturedStart()});
      return true;
    });
    QCOMPARE(seen.size(), 5);
    QCOMPARE(seen.last(), (QPair<qsizetype, qsizetype>{6, 4}));
    seen.clear();
    search::forEachLineMatch(rope, re, 1, 2, [&](qsizetype base, const QRegularExpressionMatch &m) {
      seen.append({base, m.capturedStart()});
      return true;
    }, false);
    QCOMPARE(seen.size(), 1); // the first of each line that has one
  }

  void expandReplacementDollars() {
    const QRegularExpression re(u"(\\w)(\\w)(x)?"_s);
    const QRegularExpressionMatch m = re.match(u"ab"_s);
    using search::expandReplacement;
    QCOMPARE(expandReplacement(u"$2$1"_s, m), u"ba"_s);
    QCOMPARE(expandReplacement(u"[$&]"_s, m), u"[ab]"_s);
    QCOMPARE(expandReplacement(u"$$1"_s, m), u"$1"_s);
    QCOMPARE(expandReplacement(u"$0"_s, m), u"ab"_s);
    QCOMPARE(expandReplacement(u"$3|"_s, m), u"|"_s);      // a group that did not take part
    QCOMPARE(expandReplacement(u"$4 $x $"_s, m), u"$4 $x $"_s); // no such group: as written
    QCOMPARE(expandReplacement(u"$12"_s, m), u"a2"_s);        // no group 12: $1 and a 2
    QCOMPARE(expandReplacement(u"a\\nb\\tc\\\\d\\q"_s, m), u"a\nb\tc\\d\\q"_s);
  }
};

QTEST_APPLESS_MAIN(TstTextSearch)
#include "tst_textsearch.moc"
