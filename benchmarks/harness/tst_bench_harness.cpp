#include "bench.h"
#include "datagen.h"

#include <QtTest>

using namespace qce::bench;

class TstBenchHarness : public QObject
{
    Q_OBJECT
private slots:
    void summarizeStatistics()
    {
        QList<qint64> samples;
        for (qint64 i = 1; i <= 100; ++i)
            samples.append(i * 10);
        const Result r = summarize(QStringLiteral("x"), samples, 10);
        QCOMPARE(r.iterations, 100);
        QCOMPARE(r.minNs, 10.0);
        QCOMPARE(r.medianNs, 505.0);
        QCOMPARE(r.p95Ns, 950.0);
        QCOMPARE(r.meanNs, 505.0);
        QCOMPARE(r.medianNsPerItem(), 50.5);
    }

    void runnerHonoursFilterAndIterations()
    {
        Runner runner;
        int calls = 0;
        runner.add("a/one", [&](Context &) { ++calls; }, 4, 2);
        runner.add("b/two", [&](Context &) { ++calls; }, 4, 0);
        const QList<Result> results = runner.run(QStringLiteral("^a/"));
        QCOMPARE(results.size(), 1);
        QCOMPARE(results[0].iterations, 4);
        QCOMPARE(calls, 6); // 2 warmup + 4 timed, b/two skipped
    }

    void stoppedTimerExcludesSetup()
    {
        Runner runner;
        runner.add("setup", [](Context &ctx) {
            ctx.stopTimer();
            QTest::qSleep(20);
            ctx.startTimer();
        }, 3, 0);
        const QList<Result> results = runner.run();
        QVERIFY(results[0].medianNs < 10'000'000);
    }

    void dataIsDeterministic()
    {
        for (Shape shape : {Shape::ManyShortLines, Shape::FewLongLines, Shape::OneGiantLine}) {
            const QString a = generateText(shape, 50'000, 7);
            QCOMPARE(a, generateText(shape, 50'000, 7));
            QVERIFY(a != generateText(shape, 50'000, 8));
            QCOMPARE(a.size(), 50'000);
        }
    }

    void dataShapes()
    {
        const QString shortLines = generateText(Shape::ManyShortLines, 100'000);
        QVERIFY(shortLines.count(QLatin1Char('\n')) > 1000);
        const QString giant = generateText(Shape::OneGiantLine, 100'000);
        QVERIFY(!giant.contains(QLatin1Char('\n')));
        const QString crlf = generateText(Shape::ManyShortLines, 100'000, 1, LineEnding::CrLf);
        QCOMPARE(crlf.count(QLatin1Char('\n')), crlf.count(QStringLiteral("\r\n")));
        QVERIFY(!crlf.endsWith(QLatin1Char('\r'))); // never split a CRLF pair
    }

    void incrementalMatchesWhole()
    {
        SyntheticText gen(Shape::ManyShortLines, 3);
        QString joined;
        for (int i = 0; i < 10; ++i)
            joined += gen.next(777);
        QCOMPARE(joined, generateText(Shape::ManyShortLines, joined.size(), 3));
    }
};

QTEST_APPLESS_MAIN(TstBenchHarness)
#include "tst_bench_harness.moc"
