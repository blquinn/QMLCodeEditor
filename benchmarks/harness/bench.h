#ifndef QCE_BENCH_H
#define QCE_BENCH_H

// Small benchmark runner: warmup + N timed iterations per case, min/median/p95/mean, a text table and JSON
// output for tracking over time. Qt Core only, so core benchmarks run without a window system.

#include <QtCore/QList>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <chrono>
#include <functional>

namespace qce::bench {

// Prevents the compiler from discarding a computed value.
template <class T> inline void doNotOptimize(const T &value)
{
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : "r,m"(value) : "memory");
#else
    static volatile char sink;
    sink = *reinterpret_cast<const volatile char *>(&value);
#endif
}

// Handed to each iteration of a case.
class Context
{
public:
    // Work items done per iteration (edits, lookups, ...). Reported as a per-item time as well.
    void setItems(qint64 items) { m_items = items; }
    qint64 items() const { return m_items; }

    // Exclude setup/teardown inside an iteration from the measurement.
    void stopTimer();
    void startTimer();

private:
    friend class Runner;
    using Clock = std::chrono::steady_clock;
    void begin();
    qint64 finish();

    qint64 m_items = 1;
    qint64 m_accumulatedNs = 0;
    Clock::time_point m_started;
    bool m_running = false;
};

struct Result
{
    QString name;
    int iterations = 0;
    qint64 items = 1;
    double minNs = 0;
    double medianNs = 0;
    double p95Ns = 0;
    double meanNs = 0;
    QString unit = QStringLiteral("ns"); // the *Ns fields hold this unit ("ns", "bytes", "count", ...)
    double medianNsPerItem() const { return items > 0 ? medianNs / double(items) : medianNs; }
};

// A single measured quantity that is not a duration (memory, counters): every statistic is `value`.
Result valueResult(const QString &name, double value, const QString &unit);

// Statistics over raw per-iteration times (nanoseconds). Exposed for tests.
Result summarize(const QString &name, QList<qint64> samplesNs, qint64 items);

class Runner
{
public:
    using Fn = std::function<void(Context &)>;

    void add(QString name, Fn fn, int iterations = 5, int warmup = 1);

    // Parses --filter/--json/--iterations/--list from `args` (args[0] is the program), runs, prints a table.
    // Returns the process exit code.
    int exec(const QStringList &args);

    // Runs every case whose name matches `filter` (regular expression, empty = all).
    QList<Result> run(const QString &filter = {}, int iterationsOverride = 0) const;

private:
    struct Case
    {
        QString name;
        Fn fn;
        int iterations;
        int warmup;
    };
    QList<Case> m_cases;
};

QString formatDuration(double ns);
QByteArray toJson(const QList<Result> &results, const QString &suite);

} // namespace qce::bench

#endif // QCE_BENCH_H
