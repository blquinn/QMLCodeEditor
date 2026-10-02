#include "core/linemarkerset.h"
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

} // namespace

class TstLineMarkers : public QObject {
  Q_OBJECT
private slots:
  void addAndQuery() {
    TextDocument doc;
    doc.setText(numberedLines(10));
    LineMarkerSet set(&doc);
    const int a = set.add(2, 4, 1);
    const int b = set.add(7, 7, 2, 5);
    QCOMPARE(set.size(), 2);
    QCOMPARE(set.query(0, 1).size(), 0);
    QCOMPARE(set.query(0, 2).size(), 1);
    QCOMPARE(set.query(3, 3).first().id, a);
    QCOMPARE(set.query(5, 6).size(), 0);
    const auto both = set.query(0, 9);
    QCOMPARE(both.size(), 2);
    QCOMPARE(both.last().id, b);
    QCOMPARE(both.last().priority, 5);
    QVERIFY(set.remove(a));
    QVERIFY(!set.remove(a));
    QCOMPARE(set.query(0, 9).size(), 1);
    set.clear();
    QCOMPARE(set.size(), 0);
    QCOMPARE(doc.anchors().size(), 0);
  }

  void markersFollowEdits() {
    TextDocument doc;
    doc.setText(numberedLines(10));
    LineMarkerSet set(&doc);
    const int id = set.add(4, 5);
    doc.insert(0, QStringLiteral("a\nb\n")); // two lines above
    auto m = set.marker(id);
    QCOMPARE(m.firstLine, 6);
    QCOMPARE(m.lastLine, 7);
    // A line break at the end of the last line does not extend it ...
    doc.insert(doc.rope().lineEnd(7), QStringLiteral("\n"));
    m = set.marker(id);
    QCOMPARE(m.firstLine, 6);
    QCOMPARE(m.lastLine, 7);
    // ... and one inside it does.
    doc.insert(doc.rope().lineStart(7) + 2, QStringLiteral("\n"));
    m = set.marker(id);
    QCOMPARE(m.lastLine, 8);
    // A line break at the start of the first line pushes the marker down.
    doc.insert(doc.rope().lineStart(6), QStringLiteral("\n"));
    m = set.marker(id);
    QCOMPARE(m.firstLine, 7);
    QCOMPARE(m.lastLine, 9);
    // Typing at the start of the first line stays on it.
    doc.insert(doc.rope().lineStart(7), QStringLiteral("x"));
    QCOMPARE(set.marker(id).firstLine, 7);
    // Removing every line it covers leaves it on the line the removal joined.
    doc.remove(doc.rope().lineStart(7), doc.rope().lineStart(10));
    m = set.marker(id);
    QCOMPARE(m.firstLine, m.lastLine);
    QCOMPARE(m.firstLine, 7);
  }

  void markRangeMergesNeighbours() {
    TextDocument doc;
    doc.setText(numberedLines(20));
    LineMarkerSet set(&doc);
    set.markRange(3, 3, 1);
    set.markRange(5, 5, 1);
    QCOMPARE(set.size(), 2);
    set.markRange(4, 4, 1); // bridges the two
    QCOMPARE(set.size(), 1);
    QCOMPARE(set.query(0, 19).first().firstLine, 3);
    QCOMPARE(set.query(0, 19).first().lastLine, 5);
    set.markRange(4, 4, 1); // already covered: nothing changes
    QCOMPARE(set.size(), 1);
    set.markRange(6, 6, 2); // another kind does not merge
    QCOMPARE(set.size(), 2);
    set.markRange(7, 8, 1);
    QCOMPARE(set.size(), 3);
  }

  void queryAgreesWithBruteForce() {
    std::mt19937 rng(11);
    TextDocument doc;
    doc.setText(numberedLines(400));
    LineMarkerSet set(&doc);
    auto rand = [&](qsizetype n) { return qsizetype(rng() % quint32(n)); };
    QList<int> ids;
    for (int i = 0; i < 60; ++i) {
      const qsizetype first = rand(doc.rope().lineCount());
      ids.append(set.add(first, first + (i % 7 == 0 ? rand(120) : rand(3))));
    }
    for (int step = 0; step < 300; ++step) {
      const qsizetype len = doc.length();
      const qsizetype start = rand(len + 1);
      if (rng() % 2)
        doc.insert(start, rng() % 3 ? QStringLiteral("zz") : QStringLiteral("\n\n"));
      else
        doc.remove(start, qMin(len, start + rand(30)));
      if (step % 25 != 0)
        continue;
      const qsizetype lines = doc.rope().lineCount();
      const qsizetype a = rand(lines), b = qMin(lines - 1, a + rand(20));
      QList<int> expected;
      for (int id : std::as_const(ids)) {
        const auto m = set.marker(id);
        if (m.firstLine <= b && m.lastLine >= a)
          expected.append(id);
      }
      QList<int> got;
      for (const auto &m : set.query(a, b))
        got.append(m.id);
      std::sort(expected.begin(), expected.end());
      std::sort(got.begin(), got.end());
      QCOMPARE(got, expected);
    }
  }
};

QTEST_GUILESS_MAIN(TstLineMarkers)
#include "tst_linemarkers.moc"
