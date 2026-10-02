#include "frametimer.h"

#include <QtGui/QGuiApplication>
#include <QtQuick/QQuickWindow>
#include <QtTest>

using qce::bench::FrameTimer;

class TstFrameTimer : public QObject
{
    Q_OBJECT

    // The hooks are ordinary QQuickWindow signals; emitting them directly tests the bookkeeping without
    // needing a GPU in CI.
    static void emitSignal(QQuickWindow &w, const char *name) { QVERIFY(QMetaObject::invokeMethod(&w, name)); }

private slots:
    void recordsOneFramePerSwap()
    {
        QQuickWindow window;
        FrameTimer timer(&window);
        for (int i = 0; i < 3; ++i) {
            emitSignal(window, "beforeSynchronizing");
            QTest::qSleep(2);
            emitSignal(window, "afterSynchronizing");
            emitSignal(window, "beforeRendering");
            QTest::qSleep(3);
            emitSignal(window, "afterRendering");
            emitSignal(window, "frameSwapped");
        }
        const QList<FrameTimer::Frame> frames = timer.frames();
        QCOMPARE(frames.size(), 3);
        QCOMPARE(frames[0].intervalNs, 0);
        for (const FrameTimer::Frame &f : frames) {
            QVERIFY(f.syncNs >= 2'000'000);
            QVERIFY(f.renderNs >= 3'000'000);
        }
        QVERIFY(frames[1].intervalNs >= 5'000'000);

        const auto results = timer.results(QStringLiteral("scroll"));
        QCOMPARE(results.size(), 3);
        QCOMPARE(results[0].name, QStringLiteral("scroll/sync"));
        QCOMPARE(results[0].iterations, 3);
        QCOMPARE(results[2].iterations, 2); // first frame has no interval

        timer.reset();
        QCOMPARE(timer.frameCount(), 0);
    }
};

QTEST_MAIN(TstFrameTimer)
#include "tst_frametimer.moc"
