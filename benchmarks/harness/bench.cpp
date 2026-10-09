#include "bench.h"

#include <QtCore/QCommandLineParser>
#include <QtCore/QDateTime>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QSysInfo>
#include <QtCore/QThread>
#include <QtCore/QtGlobal>

#include <algorithm>
#include <cmath>
#include <cstdio>

#if defined(__GLIBC__)
#include <malloc.h>
#endif

namespace qce::bench {

void Context::begin()
{
    m_accumulatedNs = 0;
    startTimer();
}

void Context::startTimer()
{
    if (m_running)
        return;
    m_started = Clock::now();
    m_running = true;
}

void Context::stopTimer()
{
    if (!m_running)
        return;
    m_accumulatedNs += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - m_started).count();
    m_running = false;
}

qint64 Context::finish()
{
    stopTimer();
    return m_accumulatedNs;
}

Result summarize(const QString &name, QList<qint64> samplesNs, qint64 items)
{
    Result r;
    r.name = name;
    r.items = items;
    r.iterations = int(samplesNs.size());
    if (samplesNs.isEmpty())
        return r;
    std::sort(samplesNs.begin(), samplesNs.end());
    const qsizetype n = samplesNs.size();
    r.minNs = double(samplesNs.first());
    r.medianNs = n % 2 ? double(samplesNs[n / 2]) : double(samplesNs[n / 2 - 1] + samplesNs[n / 2]) / 2.0;
    r.p95Ns = double(samplesNs[std::max<qsizetype>(0, qsizetype(std::ceil(0.95 * double(n))) - 1)]);
    double sum = 0;
    for (qint64 s : samplesNs)
        sum += double(s);
    r.meanNs = sum / double(n);
    return r;
}

void Runner::add(QString name, Fn fn, int iterations, int warmup)
{
    m_cases.append({std::move(name), std::move(fn), iterations, warmup, {}, {}});
}

void Runner::addValue(QString name, std::function<double()> fn, QString unit)
{
    m_cases.append({std::move(name), {}, 1, 0, std::move(fn), std::move(unit)});
}

QList<Result> Runner::run(const QString &filter, int iterationsOverride) const
{
    const QRegularExpression re(filter);
    QList<Result> results;
    for (const Case &c : m_cases) {
        if (!filter.isEmpty() && !re.match(c.name).hasMatch())
            continue;
        if (c.value) {
            results.append(valueResult(c.name, c.value(), c.unit));
            continue;
        }
        const int iterations = iterationsOverride > 0 ? iterationsOverride : c.iterations;
        Context ctx;
        for (int i = 0; i < c.warmup; ++i) {
            ctx.begin();
            c.fn(ctx);
            ctx.finish();
        }
        QList<qint64> samples;
        samples.reserve(iterations);
        qint64 items = 1;
        for (int i = 0; i < iterations; ++i) {
            ctx.m_items = 1;
            ctx.begin();
            c.fn(ctx);
            samples.append(ctx.finish());
            items = ctx.items();
        }
        results.append(summarize(c.name, samples, items));
    }
    return results;
}

QString formatDuration(double ns)
{
    if (ns < 1e3)
        return QString::asprintf("%.1f ns", ns);
    if (ns < 1e6)
        return QString::asprintf("%.2f us", ns / 1e3);
    if (ns < 1e9)
        return QString::asprintf("%.2f ms", ns / 1e6);
    return QString::asprintf("%.3f s", ns / 1e9);
}

static QString cpuModel()
{
    QFile f(QStringLiteral("/proc/cpuinfo"));
    if (f.open(QIODevice::ReadOnly)) {
        // readAll(): procfs files report size 0, so atEnd()/readLine() loops see nothing.
        for (const QByteArray &line : f.readAll().split('\n')) {
            if (line.startsWith("model name")) {
                const qsizetype colon = line.indexOf(':');
                if (colon >= 0)
                    return QString::fromUtf8(line.mid(colon + 1).trimmed());
            }
        }
    }
    return QSysInfo::currentCpuArchitecture();
}

static QString compilerName()
{
#if defined(__clang__)
    return QStringLiteral("clang ") + QStringLiteral(__clang_version__);
#elif defined(__GNUC__)
    return QStringLiteral("gcc ") + QStringLiteral(__VERSION__);
#elif defined(_MSC_VER)
    return QStringLiteral("msvc %1").arg(_MSC_VER);
#else
    return QStringLiteral("unknown");
#endif
}

static bool optimizedBuild()
{
#ifdef NDEBUG
    return true;
#else
    return false;
#endif
}

qint64 heapBytes()
{
#if defined(__GLIBC__) && (__GLIBC__ > 2 || __GLIBC_MINOR__ >= 33)
    const struct mallinfo2 info = mallinfo2();
    return qint64(info.uordblks) + qint64(info.hblkhd);
#else
    return -1;
#endif
}

qint64 residentBytes()
{
    QFile f(QStringLiteral("/proc/self/status"));
    if (!f.open(QIODevice::ReadOnly))
        return -1;
    // procfs reports size 0, so atEnd()/readLine() loops don't work; read it whole.
    for (const QByteArray &line : f.readAll().split('\n')) {
        if (line.startsWith("VmRSS:"))
            return line.mid(6).trimmed().split(' ').first().toLongLong() * 1024;
    }
    return -1;
}

Result valueResult(const QString &name, double value, const QString &unit)
{
    Result r;
    r.name = name;
    r.iterations = 1;
    r.minNs = r.medianNs = r.p95Ns = r.meanNs = value;
    r.unit = unit;
    return r;
}

QByteArray toJson(const QList<Result> &results, const QString &suite)
{
    QJsonObject meta;
    meta[QStringLiteral("suite")] = suite;
    meta[QStringLiteral("date")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    meta[QStringLiteral("qt")] = QString::fromLatin1(qVersion());
    meta[QStringLiteral("compiler")] = compilerName();
    meta[QStringLiteral("cpu")] = cpuModel();
    meta[QStringLiteral("cores")] = QThread::idealThreadCount();
    meta[QStringLiteral("os")] = QSysInfo::prettyProductName();
    meta[QStringLiteral("optimized")] = optimizedBuild();

    QJsonArray arr;
    for (const Result &r : results) {
        QJsonObject o;
        o[QStringLiteral("name")] = r.name;
        o[QStringLiteral("unit")] = r.unit;
        o[QStringLiteral("iterations")] = r.iterations;
        o[QStringLiteral("items")] = r.items;
        o[QStringLiteral("min_ns")] = r.minNs;
        o[QStringLiteral("median_ns")] = r.medianNs;
        o[QStringLiteral("p95_ns")] = r.p95Ns;
        o[QStringLiteral("mean_ns")] = r.meanNs;
        o[QStringLiteral("median_ns_per_item")] = r.medianNsPerItem();
        arr.append(o);
    }
    QJsonObject root;
    root[QStringLiteral("meta")] = meta;
    root[QStringLiteral("results")] = arr;
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

int Runner::exec(const QStringList &args)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("QMLCodeEditor benchmark"));
    parser.addHelpOption();
    const QCommandLineOption filterOpt({QStringLiteral("f"), QStringLiteral("filter")},
                                       QStringLiteral("Only run cases whose name matches this regex."),
                                       QStringLiteral("regex"));
    const QCommandLineOption jsonOpt({QStringLiteral("o"), QStringLiteral("json")},
                                     QStringLiteral("Write results as JSON to <file>."),
                                     QStringLiteral("file"));
    const QCommandLineOption itersOpt({QStringLiteral("n"), QStringLiteral("iterations")},
                                      QStringLiteral("Override the iteration count."), QStringLiteral("n"));
    const QCommandLineOption listOpt(QStringLiteral("list"), QStringLiteral("List case names and exit."));
    parser.addOptions({filterOpt, jsonOpt, itersOpt, listOpt});
    parser.process(args);

    if (parser.isSet(listOpt)) {
        for (const Case &c : m_cases)
            std::printf("%s\n", qPrintable(c.name));
        return 0;
    }
    if (!optimizedBuild())
        std::fprintf(stderr, "warning: unoptimized build, timings are not meaningful (use the release preset)\n");

    const QList<Result> results = run(parser.value(filterOpt), parser.value(itersOpt).toInt());
    if (results.isEmpty()) {
        std::fprintf(stderr, "no benchmark cases matched\n");
        return 1;
    }

    int width = 4;
    for (const Result &r : results)
        width = std::max(width, int(r.name.size()));
    std::printf("%-*s  %5s  %12s  %12s  %12s  %12s  %12s\n", width, "case", "iters", "min", "median", "p95",
                "mean", "median/item");
    for (const Result &r : results) {
        if (r.unit != QStringLiteral("ns")) {
            std::printf("%-*s  %5d  %12.0f %s\n", width, qPrintable(r.name), r.iterations, r.medianNs, qPrintable(r.unit));
            continue;
        }
        std::printf("%-*s  %5d  %12s  %12s  %12s  %12s  %12s\n", width, qPrintable(r.name), r.iterations,
                    qPrintable(formatDuration(r.minNs)), qPrintable(formatDuration(r.medianNs)),
                    qPrintable(formatDuration(r.p95Ns)), qPrintable(formatDuration(r.meanNs)),
                    r.items > 1 ? qPrintable(formatDuration(r.medianNsPerItem())) : "-");
    }

    if (parser.isSet(jsonOpt)) {
        QFile out(parser.value(jsonOpt));
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            std::fprintf(stderr, "cannot write %s: %s\n", qPrintable(out.fileName()),
                         qPrintable(out.errorString()));
            return 2;
        }
        const QString suite = args.isEmpty() ? QString() : QFileInfo(args.first()).baseName();
        out.write(toJson(results, suite));
    }
    return 0;
}

} // namespace qce::bench
