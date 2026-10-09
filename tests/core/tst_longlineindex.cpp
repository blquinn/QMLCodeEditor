#include "core/longlineindex.h"

#include <QtCore/QRandomGenerator>
#include <QtTest>

using namespace qce;
using namespace Qt::StringLiterals;

namespace {

// A fractional cell width makes tab-stop rounding matter.
constexpr qreal kCell = 8.5;

// x of every code point boundary, by plain iteration over the string.
QList<qreal> referenceXs(const QString &text, const WrapMeasure &measure) {
  QList<qreal> xs(text.size() + 1, 0);
  const qreal tab = measure.tabWidth() * measure.cellAdvance();
  qreal x = 0;
  for (qsizetype i = 0; i < text.size();) {
    const char32_t cp = text.at(i).isHighSurrogate() && i + 1 < text.size() && text.at(i + 1).isLowSurrogate()
                          ? QChar::surrogateToUcs4(text.at(i), text.at(i + 1))
                          : char32_t(text.at(i).unicode());
    const qsizetype n = cp > 0xffff ? 2 : 1;
    x = cp == u'\t' ? (std::floor((x + 1e-5) / tab) + 1) * tab : x + measure.advance(cp);
    for (qsizetype k = 0; k < n; ++k)
      xs[i + k + 1] = x;
    if (n == 2)
      xs[i + 1] = xs[i]; // inside a pair the x is the one before it
    i += n;
  }
  return xs;
}

QString randomLine(QRandomGenerator &rng, int length) {
  static const QList<QString> pieces = {u"a"_s, u"b"_s,  u"word"_s, u" "_s,  u"\t"_s, u"\t"_s,
                                        u"é"_s, u"日"_s, u"😀"_s,   u"x"_s, u"{"_s,  u"é"_s};
  QString text;
  while (text.size() < length)
    text += pieces[int(rng.bounded(pieces.size()))];
  return text;
}

} // namespace

class TestLongLineIndex : public QObject {
  Q_OBJECT
private slots:
  void asciiLineMatchesTheGrid() {
    const GridWrapMeasure measure(4, kCell);
    QString text;
    for (int i = 0; i < 5000; ++i)
      text += i % 37 == 0 ? u"\t"_s : u"x"_s;
    const Rope rope = Rope::fromString(text);
    const auto index = LongLineIndex::build(rope, 0, text.size(), measure);
    QString why;
    QVERIFY2(index->validate(rope, 0, measure, &why), qPrintable(why));
    QVERIFY(index->chunkCount() >= 4);
    const QList<qreal> xs = referenceXs(text, measure);
    QCOMPARE(index->width(), xs.last());
    for (qsizetype col : {0, 1, 36, 37, 38, 1023, 1024, 1025, 2047, 2048, 4999, 5000})
      QCOMPARE(index->xForColumn(rope, 0, col, measure), xs[col]);
  }

  void columnForXIsTheNearestBoundary() {
    const GridWrapMeasure measure(4, kCell);
    const QString text = u"ab\tcd日e😀f\t"_s.repeated(900);
    const Rope rope = Rope::fromString(text);
    const auto index = LongLineIndex::build(rope, 0, text.size(), measure);
    const QList<qreal> xs = referenceXs(text, measure);
    QRandomGenerator rng(5);
    for (int i = 0; i < 500; ++i) {
      const qsizetype col = qsizetype(rng.bounded(text.size() + 1));
      if (col > 0 && col < text.size() && text.at(col).isLowSurrogate())
        continue;
      // The middle of a cell is nearer to the boundary before or after; the boundary itself is exact.
      QCOMPARE(index->columnForX(rope, 0, xs[col], measure), col);
    }
    QCOMPARE(index->columnForX(rope, 0, -5, measure), 0);
    QCOMPARE(index->columnForX(rope, 0, index->width() + 100, measure), text.size());
  }

  void offsetsInsideTheDocumentAreRelativeToTheLine() {
    const GridWrapMeasure measure(4, kCell);
    const QString before = u"first line\nsecond\n"_s;
    const QString line = QString(3000, u'y') + u"\t" + QString(3000, u'z');
    const Rope rope = Rope::fromString(before + line + u"\nlast"_s);
    const auto index = LongLineIndex::build(rope, before.size(), line.size(), measure);
    QString why;
    QVERIFY2(index->validate(rope, before.size(), measure, &why), qPrintable(why));
    const QList<qreal> xs = referenceXs(line, measure);
    QCOMPARE(index->xForColumn(rope, before.size(), 3001, measure), xs[3001]);
    QCOMPARE(index->width(), xs.last());
  }

  void surrogatePairsAreNeverSplit() {
    const GridWrapMeasure measure(4, kCell);
    const QString text = u"😀"_s.repeated(3000);
    const Rope rope = Rope::fromString(text);
    const auto index = LongLineIndex::build(rope, 0, text.size(), measure);
    for (qsizetype c = 0; c < index->chunkCount(); ++c)
      QVERIFY2(index->chunkStart(c) % 2 == 0, "a chunk starts inside a pair");
    // A column inside a pair gives the x before it.
    QCOMPARE(index->xForColumn(rope, 0, 3, measure), index->xForColumn(rope, 0, 2, measure));
  }

  void edits_matchAFreshIndex_data() {
    QTest::addColumn<int>("seed");
    for (int seed = 1; seed <= 6; ++seed)
      QTest::addRow("seed %d", seed) << seed;
  }
  void edits_matchAFreshIndex() {
    QFETCH(int, seed);
    const GridWrapMeasure measure(4, kCell);
    QRandomGenerator rng{quint32(seed)};
    QString text = randomLine(rng, 12000);
    Rope rope = Rope::fromString(text);
    auto index = LongLineIndex::build(rope, 0, text.size(), measure);
    for (int step = 0; step < 150; ++step) {
      // Edits of all sizes, at the start, the end and in the middle; nothing that splits a pair.
      qsizetype at = step % 7 == 0 ? 0 : step % 11 == 0 ? text.size() : qsizetype(rng.bounded(text.size() + 1));
      if (at > 0 && at < text.size() && text.at(at).isLowSurrogate())
        --at;
      qsizetype removed = rng.bounded(4) == 0 ? qsizetype(rng.bounded(3000)) : qsizetype(rng.bounded(6));
      removed = qMin(removed, text.size() - at);
      if (at + removed < text.size() && text.at(at + removed).isLowSurrogate())
        ++removed;
      const QString added = rng.bounded(5) == 0 ? randomLine(rng, int(rng.bounded(2500))) : randomLine(rng, int(rng.bounded(4)));
      if (text.size() - removed + added.size() < 1)
        continue;
      text.replace(at, removed, added);
      rope = rope.replace(at, at + removed, added);
      index = index->afterEdit(rope, 0, at, removed, added.size(), measure);

      QString why;
      QVERIFY2(index->validate(rope, 0, measure, &why), qPrintable(QStringLiteral("step %1: ").arg(step) + why));
      QCOMPARE(index->length(), text.size());
      const QList<qreal> xs = referenceXs(text, measure);
      QVERIFY2(qAbs(index->width() - xs.last()) < 1e-3, qPrintable(QStringLiteral("step %1 width").arg(step)));
      for (int probe = 0; probe < 20; ++probe) {
        const qsizetype col = qsizetype(rng.bounded(text.size() + 1));
        QVERIFY2(
          qAbs(index->xForColumn(rope, 0, col, measure) - xs[col]) < 1e-3,
          qPrintable(QStringLiteral("step %1 col %2").arg(step).arg(col))
        );
      }
    }
  }

  void typingDoesNotLeaveATrailOfTinyChunks() {
    const GridWrapMeasure measure(4, kCell);
    QString text(20000, u'a');
    Rope rope = Rope::fromString(text);
    auto index = LongLineIndex::build(rope, 0, text.size(), measure);
    // Typing in one place keeps filling one chunk; every other chunk must stay about the same size.
    for (int i = 0; i < 4000; ++i) {
      rope = rope.insert(10000, u"b");
      index = index->afterEdit(rope, 0, 10000, 0, 1, measure);
    }
    // Backspacing the same place away again.
    for (int i = 0; i < 3500; ++i) {
      rope = rope.remove(10000, 10001);
      index = index->afterEdit(rope, 0, 10000, 1, 0, measure);
    }
    QCOMPARE(index->length(), 20500);
    QVERIFY2(index->chunkCount() <= 20500 / (LongLineIndex::ChunkUnits / 2), qPrintable(QString::number(index->chunkCount())));
    QString why;
    QVERIFY2(index->validate(rope, 0, measure, &why), qPrintable(why));
  }

  void anEmptiedLineKeepsOneChunk() {
    const GridWrapMeasure measure(4, kCell);
    const QString text(3000, u'a');
    Rope rope = Rope::fromString(text);
    auto index = LongLineIndex::build(rope, 0, text.size(), measure);
    rope = rope.remove(0, text.size());
    index = index->afterEdit(rope, 0, 0, text.size(), 0, measure);
    QCOMPARE(index->length(), 0);
    QCOMPARE(index->width(), 0.0);
    QCOMPARE(index->chunkCount(), 1);
  }
};

QTEST_APPLESS_MAIN(TestLongLineIndex)
#include "tst_longlineindex.moc"
