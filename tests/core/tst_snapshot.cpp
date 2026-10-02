#include "core/textsnapshot.h"
#include "testutil.h"

#include <QtConcurrent>
#include <QtTest>

#include <atomic>

using namespace qce;

class TstSnapshot : public QObject {
  Q_OBJECT
private slots:
  void unchangedByLaterEdits() {
    test::Random rnd(1);
    const QString text = rnd.text(50'000);
    Rope rope = Rope::fromString(text);
    const TextSnapshot snap(rope, 1);
    for (int i = 0; i < 100; ++i)
      rope = rope.insert(rnd.below(int(rope.length())), rnd.text(20))
               .remove(rnd.below(1000), 1000 + rnd.below(500));
    QCOMPARE(snap.version(), 1u);
    QCOMPARE(snap.toString(), text);
    QCOMPARE(snap.lineCount(), text.count(QLatin1Char('\n')) + 1);
  }

  void copyIsConstantTime() {
    const TextSnapshot big(Rope::fromString(test::Random(2).text(2'000'000)), 1);
    const TextSnapshot copy = big;
    QVERIFY(copy.rope().sharesRootWith(big.rope()));
  }

  // Meaningful under the tsan preset: readers walk snapshots while the writer edits.
  void readersRaceWithWriter() {
    test::Random rnd(3);
    Rope rope = Rope::fromString(rnd.text(200'000));
    std::atomic<bool> stop{false};
    std::atomic<int> bad{0};
    QMutex mutex;
    TextSnapshot published(rope, 0);

    auto reader = [&] {
      while (!stop) {
        TextSnapshot s;
        {
          QMutexLocker lock(&mutex);
          s = published;
        }
        // A snapshot must be internally consistent: its text matches its own summary.
        const QString t = s.toString();
        if (t.size() != s.length() || t.count(QLatin1Char('\n')) + 1 != s.lineCount() || !s.rope().validate())
          ++bad;
      }
    };
    QList<QFuture<void>> readers;
    for (int i = 0; i < 3; ++i)
      readers << QtConcurrent::run(reader);
    for (int i = 1; i <= 300; ++i) {
      rope = rope.insert(rnd.below(int(rope.length())), rnd.text(rnd.range(1, 100)));
      if (rnd.chance(40))
        rope = rope.remove(rnd.below(int(rope.length())), rnd.below(int(rope.length())));
      QMutexLocker lock(&mutex);
      published = TextSnapshot(rope, quint64(i));
    }
    stop = true;
    for (auto &f : readers)
      f.waitForFinished();
    QCOMPARE(bad.load(), 0);
  }
};

QTEST_APPLESS_MAIN(TstSnapshot)
#include "tst_snapshot.moc"
