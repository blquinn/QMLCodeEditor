#include "core/rope.h"
#include "testutil.h"

#include <QtTest>

using namespace qce;

namespace {

// Reference model: a QString with the ADR 0007 line rules, computed the slow way.
struct Model {
  QString text;
  QList<qsizetype> starts{0};

  explicit Model(const QString &t) : text(t) {
    for (qsizetype i = 0; i < text.size(); ++i)
      if (text[i] == QLatin1Char('\n'))
        starts.append(i + 1);
  }
  qsizetype lineCount() const { return starts.size(); }
  qsizetype end(qsizetype line) const {
    if (line == lineCount() - 1)
      return text.size();
    qsizetype e = starts[line + 1] - 1;
    if (e > starts[line] && text[e - 1] == QLatin1Char('\r'))
      --e;
    return e;
  }
  qsizetype lineOf(qsizetype offset) const {
    return std::upper_bound(starts.begin(), starts.end(), offset) - starts.begin() - 1;
  }
  TextPosition position(qsizetype offset) const {
    if (
      offset > 0 && offset < text.size() && text[offset - 1].isHighSurrogate() &&
      text[offset].isLowSurrogate()
    )
      --offset;
    if (
      offset > 0 && offset < text.size() && text[offset - 1] == QLatin1Char('\r') &&
      text[offset] == QLatin1Char('\n')
    )
      --offset;
    const qsizetype l = lineOf(offset);
    return {l, offset - starts[l]};
  }
};

void compareAll(const Rope &r, const Model &m, test::Random &rnd, int samples) {
  QCOMPARE(r.lineCount(), m.lineCount());
  for (int i = 0; i < samples; ++i) {
    const qsizetype line = rnd.below(int(m.lineCount()));
    QCOMPARE(r.lineStart(line), m.starts[line]);
    QCOMPARE(r.lineEnd(line), m.end(line));
    QCOMPARE(r.lineLength(line), m.end(line) - m.starts[line]);
    const qsizetype off = rnd.below(int(m.text.size()) + 1);
    QCOMPARE(r.lineAt(off), m.lineOf(off));
    const TextPosition p = r.positionAt(off);
    QCOMPARE(p, m.position(off));
    // a position converts back to an offset that maps to the same position
    QCOMPARE(r.positionAt(r.offsetAt(p)), p);
    const qsizetype col = rnd.below(40);
    QCOMPARE(r.offsetAt({line, col}), m.starts[line] + qMin(col, m.end(line) - m.starts[line]));
  }
}

} // namespace

class TstPositions : public QObject {
  Q_OBJECT
private slots:
  void emptyDocument() {
    const Rope r;
    QCOMPARE(r.lineCount(), 1);
    QCOMPARE(r.lineStart(0), 0);
    QCOMPARE(r.lineEnd(0), 0);
    QCOMPARE(r.positionAt(5), (TextPosition{0, 0}));
    QCOMPARE(r.offsetAt({3, 3}), 0);
  }

  void lineBreakRules() {
    const Rope r = Rope::fromString(u"ab\r\ncd\nef\r");
    QCOMPARE(r.lineCount(), 3);
    QCOMPARE(r.lineEnd(0), 2); // CR excluded
    QCOMPARE(r.lineStart(1), 4);
    QCOMPARE(r.lineEnd(1), 6);
    QCOMPARE(r.lineStart(2), 7);
    QCOMPARE(r.lineEnd(2), 10);                      // trailing lone CR is content
    QCOMPARE(r.positionAt(3), (TextPosition{0, 2})); // between CR and LF
    QCOMPARE(r.positionAt(4), (TextPosition{1, 0}));
    QCOMPARE(r.offsetAt({0, 99}), 2);
    QCOMPARE(r.offsetAt({99, 99}), 10);
    QCOMPARE(r.offsetAt({-1, -1}), 0);
    QCOMPARE(Rope::fromString(u"a\n").lineCount(), 2);
    QCOMPARE(Rope::fromString(u"a\n").lineLength(1), 0);
  }

  void surrogateSnapping() {
    const Rope r = Rope::fromString(u"a\U0001F600b");
    QCOMPARE(r.snapToCodePoint(2), 1);
    QCOMPARE(r.snapToCodePoint(2, Rope::Snap::Forward), 3);
    QCOMPARE(r.snapToCodePoint(1), 1);
    QCOMPARE(r.snapToCodePoint(3), 3);
    QCOMPARE(r.snapToCodePoint(99), 4);
    QCOMPARE(r.positionAt(2), (TextPosition{0, 1}));
  }

  void matchesModelAfterBulkBuild() {
    test::Random rnd(test::testSeed());
    const QString text = rnd.text(400'000);
    compareAll(Rope::fromString(text), Model(text), rnd, 3000);
  }

  void matchesModelAfterEdits() {
    test::Random rnd(test::testSeed(11));
    QString text = rnd.text(100'000);
    Rope r = Rope::fromString(text);
    for (int i = 0; i < test::testIterations(150); ++i) {
      const int at = rnd.below(int(text.size()) + 1);
      if (rnd.chance(50)) {
        const QString t = rnd.text(rnd.range(1, 3000));
        text.insert(at, t);
        r = r.insert(at, t);
      } else {
        const int e = qMin<int>(int(text.size()), at + rnd.range(0, 3000));
        text.remove(at, e - at);
        r = r.remove(at, e);
      }
      compareAll(r, Model(text), rnd, 30);
    }
  }
};

QTEST_APPLESS_MAIN(TstPositions)
#include "tst_positions.moc"
