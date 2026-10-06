#include "core/decorationset.h"
#include "core/textdocument.h"

#include <QtTest>

#include <random>

using namespace qce;

namespace {

QString numberedLines(int count) {
  QString text;
  for (int i = 0; i < count; ++i)
    text += QStringLiteral("line %1\n").arg(i);
  return text;
}

DecorationSpec spec(qsizetype start, qsizetype end, DecorationKind kind = DecorationKind::Squiggle, int priority = 0) {
  DecorationSpec s;
  s.start = start;
  s.end = end;
  s.kind = kind;
  s.priority = priority;
  return s;
}

} // namespace

class TstDecorationSet : public QObject {
  Q_OBJECT
private slots:
  void addAndQuery() {
    TextDocument doc;
    doc.setText(numberedLines(10)); // "line N\n" is 7 units for N < 10
    DecorationSet set(&doc);
    const int a = set.add(spec(2, 5));
    const int b = set.add(spec(30, 33, DecorationKind::Background));
    QCOMPARE(set.size(), 2);
    QCOMPARE(set.count(DecorationKind::Squiggle), 1);
    QCOMPARE(set.count(DecorationKind::Background), 1);
    QCOMPARE(set.query(0, 1).size(), 0);
    QCOMPARE(set.query(0, 2).size(), 1); // touching counts
    QCOMPARE(set.query(5, 29).first().id, a);
    QCOMPARE(set.query(6, 29).size(), 0);
    QCOMPARE(set.query(0, 100).size(), 2);
    QCOMPARE(set.query(0, 100, decorationKindBit(DecorationKind::Background)).first().id, b);
    QCOMPARE(set.queryLines(4, 4).first().id, b); // line 4 is [28, 34)
    QVERIFY(set.contains(a));
    QVERIFY(set.remove(a));
    QVERIFY(!set.remove(a));
    QVERIFY(!set.contains(a));
    QCOMPARE(set.query(0, 100).size(), 1);
    QVERIFY(set.validate());
    set.clear();
    QCOMPARE(set.size(), 0);
    QCOMPARE(doc.anchors().size(), 0);
  }

  void clampsToDocument() {
    TextDocument doc;
    doc.setText(QStringLiteral("abc"));
    DecorationSet set(&doc);
    set.add(spec(-5, 99));
    const Decoration d = set.query(0, 3).first();
    QCOMPARE(d.start, 0);
    QCOMPARE(d.end, 3);
  }

  void followsEdits() {
    TextDocument doc;
    doc.setText(QStringLiteral("0123456789"));
    DecorationSet set(&doc);
    const int id = set.add(spec(4, 7));
    doc.insert(0, u"xx");
    QCOMPARE(set.decoration(id).start, 6);
    QCOMPARE(set.decoration(id).end, 9);
    // Typing exactly at either edge leaves the decoration alone.
    doc.insert(6, u"A");
    QCOMPARE(set.decoration(id).start, 7);
    QCOMPARE(set.decoration(id).end, 10);
    doc.insert(10, u"B");
    QCOMPARE(set.decoration(id).start, 7);
    QCOMPARE(set.decoration(id).end, 10);
    // Typing inside grows it.
    doc.insert(8, u"CC");
    QCOMPARE(set.decoration(id).end, 12);
    // Deleting all of it leaves an empty decoration where it was.
    doc.remove(7, 12);
    const Decoration d = set.decoration(id);
    QCOMPARE(d.start, d.end);
    QVERIFY(set.validate());
  }

  void gravityOverride() {
    TextDocument doc;
    doc.setText(QStringLiteral("0123456789"));
    DecorationSet set(&doc);
    DecorationSpec s = spec(4, 7);
    s.startGravity = Gravity::Left;
    s.endGravity = Gravity::Right;
    const int id = set.add(s);
    doc.insert(4, u"ab");
    QCOMPARE(set.decoration(id).start, 4); // the new text is inside
    doc.insert(set.decoration(id).end, u"c");
    QCOMPARE(set.decoration(id).end, 10); // and so is text typed at the end
  }

  void mixedGravityKeepsOrder() {
    TextDocument doc;
    doc.setText(QString(40, u'x'));
    DecorationSet set(&doc);
    // Starts at 5 (leans right) and at 6 (leans left): replacing [4, 7) puts them on opposite
    // sides of the new text.
    set.add(spec(5, 8));
    DecorationSpec left = spec(6, 9);
    left.startGravity = Gravity::Left;
    set.add(left);
    set.add(spec(20, 22));
    doc.replace(4, 7, u"YYYY");
    QVERIFY(set.validate());
    const auto all = set.query(0, 100);
    QCOMPARE(all.size(), 3);
    for (qsizetype i = 1; i < all.size(); ++i)
      QVERIFY(all[i - 1].start <= all[i].start);
  }

  void randomEditsStaySorted() {
    TextDocument doc;
    doc.setText(numberedLines(200));
    DecorationSet set(&doc);
    std::mt19937 rng(7);
    auto pick = [&](qsizetype n) { return qsizetype(rng() % quint32(n + 1)); };
    QList<DecorationSpec> specs;
    for (int i = 0; i < 500; ++i) {
      DecorationSpec s = spec(pick(doc.length()), 0);
      s.end = s.start + pick(30);
      s.startGravity = (rng() & 1) ? Gravity::Left : Gravity::Right;
      specs.append(s);
    }
    set.setLayer(1, specs);
    for (int i = 0; i < 300; ++i) {
      const qsizetype at = pick(doc.length());
      if (rng() & 1)
        doc.insert(at, QStringLiteral("zz\n"));
      else
        doc.remove(at, qMin(doc.length(), at + pick(12)));
      QVERIFY(set.validate());
    }
    // A brute-force count agrees with the query.
    const auto all = set.query(0, doc.length());
    QCOMPARE(all.size(), set.size());
    const qsizetype lo = doc.length() / 3, hi = doc.length() / 2;
    qsizetype expected = 0;
    for (const Decoration &d : all)
      expected += d.start <= hi && d.end >= lo;
    QCOMPARE(set.query(lo, hi).size(), expected);
  }

  void longRangesAreFoundWithoutSlowingQueries() {
    TextDocument doc;
    doc.setText(numberedLines(30000)); // about 240k units
    DecorationSet set(&doc);
    const int whole = set.add(spec(0, doc.length(), DecorationKind::Background));
    const int small = set.add(spec(200000, 200004));
    const auto hit = set.query(199990, 200010);
    QCOMPARE(hit.size(), 2);
    QCOMPARE(hit.first().id, whole);
    QCOMPARE(hit.last().id, small);
    QVERIFY(set.validate());
    doc.insert(0, u"abc");
    QCOMPARE(set.decoration(whole).end, doc.length());
    QVERIFY(set.remove(whole));
    QCOMPARE(set.query(0, doc.length()).size(), 1);
  }

  void layersReplaceAtomically() {
    TextDocument doc;
    doc.setText(numberedLines(10));
    DecorationSet set(&doc);
    const int other = set.add(spec(1, 2), 7);
    set.setLayer(1, {spec(10, 12), spec(3, 4), spec(20, 22)});
    QCOMPARE(set.layerSize(1), 3);
    QCOMPARE(set.size(), 4);
    const auto sorted = set.query(0, 100);
    for (qsizetype i = 1; i < sorted.size(); ++i)
      QVERIFY(sorted[i - 1].start <= sorted[i].start);
    set.setLayer(1, {spec(40, 41)});
    QCOMPARE(set.layerSize(1), 1);
    QVERIFY(set.contains(other));
    QCOMPARE(set.size(), 2);
    QVERIFY(set.validate());
    set.clearLayer(1);
    QCOMPARE(set.size(), 1);
    QCOMPARE(doc.anchors().size(), 2); // the other layer's pair
  }

  void changedSignalReportsLinesAndKinds() {
    TextDocument doc;
    doc.setText(numberedLines(10));
    DecorationSet set(&doc);
    QSignalSpy spy(&set, &DecorationSet::changed);
    set.add(spec(14, 16, DecorationKind::EndOfLineText)); // line 2
    QCOMPARE(spy.size(), 1);
    QCOMPARE(spy.last().at(0).value<qsizetype>(), 2);
    QCOMPARE(spy.last().at(1).value<qsizetype>(), 2);
    QCOMPARE(spy.last().at(2).toUInt(), decorationKindBit(DecorationKind::EndOfLineText));
    doc.insert(0, u"x"); // moving decorations by editing is silent
    QCOMPARE(spy.size(), 1);
    set.clear();
    QCOMPARE(spy.size(), 2);
  }

  void resetClearsEverything() {
    TextDocument doc;
    doc.setText(numberedLines(10));
    DecorationSet set(&doc);
    set.add(spec(1, 2));
    QSignalSpy spy(&set, &DecorationSet::changed);
    doc.setText(QStringLiteral("new"));
    QCOMPARE(set.size(), 0);
    QCOMPARE(doc.anchors().size(), 0);
    QCOMPARE(spy.size(), 1);
  }

  void nextAndPrevious() {
    TextDocument doc;
    doc.setText(numberedLines(10));
    DecorationSet set(&doc);
    set.add(spec(10, 12, DecorationKind::Squiggle, 1));
    set.add(spec(20, 22, DecorationKind::Background, 2));
    set.add(spec(30, 32, DecorationKind::Squiggle, 3));
    set.add(spec(0, doc.length(), DecorationKind::Squiggle, 9)); // long ones take part too
    auto squiggle = [](const Decoration &d) { return d.kind == DecorationKind::Squiggle && d.priority < 9; };
    QCOMPARE(set.next(0, squiggle)->start, 10);
    QCOMPARE(set.next(10, squiggle)->start, 30);
    QVERIFY(!set.next(30, squiggle));
    QCOMPARE(set.previous(30, squiggle)->start, 10);
    QVERIFY(!set.previous(10, squiggle));
    auto any = [](const Decoration &) { return true; };
    QCOMPARE(set.next(0, any)->start, 10);
    QCOMPARE(set.previous(5, any)->start, 0); // the long one starts at 0
  }
};

QTEST_GUILESS_MAIN(TstDecorationSet)
#include "tst_decorationset.moc"
