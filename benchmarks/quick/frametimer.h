#ifndef QCE_FRAMETIMER_H
#define QCE_FRAMETIMER_H

// Records per-frame sync and render cost from a QQuickWindow, plus the interval between presented frames.
// All hooks use direct connections so they run on whichever thread drives the scene graph (the render thread
// with the threaded loop); storage is mutex-protected so frames() can be read from any thread.

#include "bench.h"

#include <QtCore/QList>
#include <QtCore/QMutex>
#include <QtCore/QObject>

#include <chrono>

class QQuickWindow;

namespace qce::bench {

class FrameTimer : public QObject
{
    Q_OBJECT
public:
    struct Frame
    {
        qint64 syncNs = 0;     // GUI-thread-blocked synchronization of the item tree
        qint64 renderNs = 0;   // scene-graph render (beforeRendering .. afterRendering)
        qint64 intervalNs = 0; // time since the previous presented frame; 0 for the first frame
    };

    explicit FrameTimer(QQuickWindow *window);

    QList<Frame> frames() const;
    qsizetype frameCount() const;
    void reset();

    // Statistics as harness results named "<prefix>/sync", "<prefix>/render", "<prefix>/interval", so they can
    // be written with qce::bench::toJson().
    QList<Result> results(const QString &prefix) const;

private:
    using Clock = std::chrono::steady_clock;
    static qint64 since(Clock::time_point t) { return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t).count(); }

    void onBeforeSync();
    void onAfterSync();
    void onBeforeRender();
    void onAfterRender();
    void onFrameSwapped();

    mutable QMutex m_mutex;
    QList<Frame> m_frames;
    Clock::time_point m_syncStart;
    Clock::time_point m_renderStart;
    Clock::time_point m_lastSwap;
    bool m_haveLastSwap = false;
    Frame m_current;
};

} // namespace qce::bench

#endif // QCE_FRAMETIMER_H
