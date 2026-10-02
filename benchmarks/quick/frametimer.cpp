#include "frametimer.h"

#include <QtCore/QMutexLocker>
#include <QtQuick/QQuickWindow>

namespace qce::bench {

FrameTimer::FrameTimer(QQuickWindow *window) : QObject(window)
{
    connect(window, &QQuickWindow::beforeSynchronizing, this, &FrameTimer::onBeforeSync, Qt::DirectConnection);
    connect(window, &QQuickWindow::afterSynchronizing, this, &FrameTimer::onAfterSync, Qt::DirectConnection);
    connect(window, &QQuickWindow::beforeRendering, this, &FrameTimer::onBeforeRender, Qt::DirectConnection);
    connect(window, &QQuickWindow::afterRendering, this, &FrameTimer::onAfterRender, Qt::DirectConnection);
    connect(window, &QQuickWindow::frameSwapped, this, &FrameTimer::onFrameSwapped, Qt::DirectConnection);
}

void FrameTimer::onBeforeSync()
{
    QMutexLocker lock(&m_mutex);
    m_syncStart = Clock::now();
}

void FrameTimer::onAfterSync()
{
    QMutexLocker lock(&m_mutex);
    m_current.syncNs = since(m_syncStart);
}

void FrameTimer::onBeforeRender()
{
    QMutexLocker lock(&m_mutex);
    m_renderStart = Clock::now();
}

void FrameTimer::onAfterRender()
{
    QMutexLocker lock(&m_mutex);
    m_current.renderNs = since(m_renderStart);
}

void FrameTimer::onFrameSwapped()
{
    QMutexLocker lock(&m_mutex);
    const Clock::time_point now = Clock::now();
    m_current.intervalNs = m_haveLastSwap ? std::chrono::duration_cast<std::chrono::nanoseconds>(now - m_lastSwap).count() : 0;
    m_lastSwap = now;
    m_haveLastSwap = true;
    m_frames.append(m_current);
    m_current = {};
}

QList<FrameTimer::Frame> FrameTimer::frames() const
{
    QMutexLocker lock(&m_mutex);
    return m_frames;
}

qsizetype FrameTimer::frameCount() const
{
    QMutexLocker lock(&m_mutex);
    return m_frames.size();
}

void FrameTimer::reset()
{
    QMutexLocker lock(&m_mutex);
    m_frames.clear();
    m_current = {};
    m_haveLastSwap = false;
}

QList<Result> FrameTimer::results(const QString &prefix) const
{
    const QList<Frame> all = frames();
    QList<qint64> sync, render, interval;
    for (qsizetype i = 0; i < all.size(); ++i) {
        sync.append(all[i].syncNs);
        render.append(all[i].renderNs);
        if (i > 0) // the first frame has no predecessor
            interval.append(all[i].intervalNs);
    }
    return {summarize(prefix + QStringLiteral("/sync"), sync, 1),
            summarize(prefix + QStringLiteral("/render"), render, 1),
            summarize(prefix + QStringLiteral("/interval"), interval, 1)};
}

} // namespace qce::bench
