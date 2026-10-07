// Scrolling and frame-time benchmark (M2 exit criteria): opens large generated files in the real editor item
// and scrolls through them, recording per-frame sync, render and polish cost, presented-frame intervals, how
// much layout and node churn scrolling causes, and memory growth.
//
//   bench_scroll [--json out.json] [--frames N] [--quick] [--filter REGEX] [--gutter] [--relative] [--wrap]
//                [--folds] [--diagnostics N] [--dense]
//
// --gutter adds line numbers, a change column and a marker column (M5); --relative makes the numbers
// relative to the cursor; --wrap wraps at the viewport. The "cursor" scenarios move the cursor one line per
// frame, which renumbers every row in relative mode. --folds folds 20,000 regions spread through the file
// (M7) once it is open; with --gutter the fold column joins the others. --diagnostics N pushes N diagnostics
// (squiggles, gutter icons and end-of-line messages, M9) once the file is open, spread through the whole file
// or, with --dense, one per line in the stretch around the middle where scrolling happens.
//
// Inputs are generated once into $QCE_BENCH_DIR (default: the system temp dir) and reused. Run from a release
// build on a real display for meaningful numbers; --quick uses a small file and few frames (the ctest smoke
// variant).
#include "bench.h"
#include "datagen.h"
#include "frametimer.h"
#include "quick/changecolumn.h"
#include "quick/decorationcolumn.h"
#include "quick/foldcolumn.h"
#include "quick/codeeditor.h"
#include "quick/linenumbercolumn.h"
#include "quick/markercolumn.h"

#include <QtCore/QCommandLineParser>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QEventLoop>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QRegularExpression>
#include <QtCore/QSaveFile>
#include <QtCore/QTimer>
#include <QtGui/QGuiApplication>
#include <QtGui/QSurfaceFormat>
#include <QtQuick/QQuickView>
#include <QtQuick/QQuickWindow>

#include <algorithm>
#include <cstdio>
#include <random>

using namespace qce::bench;

namespace {

QString benchDir() {
  const QString dir = qEnvironmentVariable("QCE_BENCH_DIR", QDir::tempPath());
  QDir().mkpath(dir);
  return dir;
}

// Creates `name` in the benchmark directory unless a big-enough file is already there.
QString inputFile(const QString &name, Shape shape, qint64 bytes) {
  const QString path = benchDir() + QLatin1Char('/') + name;
  if (QFileInfo(path).size() < bytes * 99 / 100) {
    const QString err = writeSyntheticFile(path, shape, bytes, 1);
    if (!err.isEmpty())
      qFatal("cannot create benchmark input: %s", qPrintable(err));
  }
  return path;
}

qint64 residentBytes() {
  QFile f(QStringLiteral("/proc/self/status"));
  if (!f.open(QIODevice::ReadOnly))
    return 0;
  // procfs reports size 0, so atEnd()/readLine() loops don't work; read it whole.
  const QList<QByteArray> lines = f.readAll().split('\n');
  for (const QByteArray &line : lines) {
    if (line.startsWith("VmRSS:"))
      return line.mid(6).trimmed().split(' ').first().toLongLong() * 1024;
  }
  return 0;
}

enum class Mode {
  Smooth, // 2.5 rows per frame, like a fast trackpad scroll
  Fling,  // 12 rows per frame, like a scrollbar drag or a page-down repeat
  Jumps,  // a random position every frame: every layout is cold
  Cursor, // the cursor moves one line down per frame, and the view follows
};

struct Scenario {
  QString name;
  QString path;
  Mode mode;
};

class Bench {
public:
  Bench(QQuickView &view, CodeEditor &editor, int frames)
      : m_view(view), m_editor(editor), m_frames(frames), m_timer(&view) {
    // frameSwapped is how scrolling is paced: one step per presented frame.
    QObject::connect(&view, &QQuickWindow::frameSwapped, &view, [this] { onFrame(); }, Qt::QueuedConnection);
  }

  // Opens `path` and records time to the first frame showing text and to the end of the load.
  QList<Result> open(const QString &label, const QString &path) {
    QList<Result> out;
    const qint64 rssBefore = residentBytes();
    m_editor.setContentY(0);
    m_firstFrameNs = -1;
    m_textSeen = false;
    m_phase = Phase::Loading;
    m_openClock.start();
    // The load ends the loop; a failure ends it too so a missing file can't hang the run.
    auto quit = [this] {
      if (m_loop)
        m_loop->quit();
    };
    const auto done = QObject::connect(m_editor.document(), &qce::TextDocument::loadFinished, &m_view, quit);
    const auto failed =
      QObject::connect(m_editor.document(), &qce::TextDocument::loadFailed, &m_view, [&](const QString &e) {
        qFatal("load failed: %s", qPrintable(e));
      });
    m_editor.load(QUrl::fromLocalFile(path));
    runLoop();
    QObject::disconnect(done);
    QObject::disconnect(failed);
    const qint64 loaded = m_openClock.nsecsElapsed();
    // Small files finish loading before any frame shows them; wait for the first one that does.
    QElapsedTimer wait;
    wait.start();
    while (m_firstFrameNs < 0 && wait.elapsed() < 5000)
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    settle();
    out << valueResult(
      label + QStringLiteral("/open/first_frame"), double(m_firstFrameNs), QStringLiteral("ns")
    );
    out << valueResult(label + QStringLiteral("/open/load_complete"), double(loaded), QStringLiteral("ns"));
    m_rssAfterLoad = residentBytes();
    out << valueResult(
      label + QStringLiteral("/memory/rope_bytes"), double(m_editor.document()->length() * 2),
      QStringLiteral("bytes")
    );
    out << valueResult(
      label + QStringLiteral("/memory/rss_growth_load"), double(m_rssAfterLoad - rssBefore),
      QStringLiteral("bytes")
    );
    out << valueResult(
      label + QStringLiteral("/lines"), double(m_editor.lineCount()), QStringLiteral("count")
    );
    return out;
  }

  QList<Result> scroll(const QString &label, Mode mode) {
    QList<Result> out;
    const qreal lh = m_editor.metrics().lineHeight();
    const qreal range = qMax<qreal>(0, m_editor.contentHeight() - m_editor.height());
    m_mode = mode;
    m_step = (mode == Mode::Smooth ? 2.5 : mode == Mode::Cursor ? 1.0 : 12.0) * lh;
    m_rng.seed(42);
    m_y = range / 2; // scroll from the middle: far from row 0, where float precision matters
    m_range = range;
    m_editor.setContentY(m_y);
    settle();

    const auto before = m_editor.renderStats();
    const qint64 rssBefore = residentBytes();
    m_timer.reset();
    m_frameNo = 0;
    m_phase = Phase::Scrolling;
    advance();
    runLoop();
    const auto after = m_editor.renderStats();

    out << m_timer.results(label + QStringLiteral("/frame"));
    if (const QString dump = qEnvironmentVariable("QCE_BENCH_DUMP_FRAMES"); !dump.isEmpty()) {
      // Per-frame CSV (sync, render, interval in ns) for looking at the pattern behind the statistics.
      QFile f(
        dump + QLatin1Char('.') + QString(label).replace(QLatin1Char('/'), QLatin1Char('_')) +
        QStringLiteral(".csv")
      );
      if (f.open(QIODevice::WriteOnly)) {
        f.write("sync_ns,render_ns,interval_ns\n");
        for (const auto &fr : m_timer.frames())
          f.write(
            QByteArray::number(fr.syncNs) + ',' + QByteArray::number(fr.renderNs) + ',' +
            QByteArray::number(fr.intervalNs) + '\n'
          );
      }
    }
    // A frame is "dropped" when it took over 1.5 display intervals; the median interval is the display's
    // refresh period when rendering keeps up.
    {
      QList<qint64> intervals;
      const auto recorded = m_timer.frames();
      for (qsizetype i = 1; i < recorded.size(); ++i)
        intervals << recorded[i].intervalNs;
      std::sort(intervals.begin(), intervals.end());
      const qint64 period = intervals.isEmpty() ? 0 : intervals[intervals.size() / 2];
      const double dropped = double(std::count_if(intervals.begin(), intervals.end(), [&](qint64 v) {
        return double(v) > 1.5 * double(period);
      }));
      out << valueResult(
        label + QStringLiteral("/frame/refresh_period"), double(period), QStringLiteral("ns")
      );
      out << valueResult(label + QStringLiteral("/frame/dropped"), dropped, QStringLiteral("count"));
      out << valueResult(
        label + QStringLiteral("/frame/dropped_pct"),
        intervals.isEmpty() ? 0 : 100.0 * dropped / double(intervals.size()), QStringLiteral("percent")
      );
    }
    const double frames = double(qMax<quint64>(1, after.polishCalls - before.polishCalls));
    out << valueResult(
      label + QStringLiteral("/polish_mean"), double(after.polishNs - before.polishNs) / frames,
      QStringLiteral("ns")
    );
    out << valueResult(
      label + QStringLiteral("/polish_max"), double(after.polishMaxNs), QStringLiteral("ns")
    );
    const double scrolled = double(qMax(1, m_frames));
    out << valueResult(
      label + QStringLiteral("/layouts_per_frame"),
      double(after.layoutsCreated - before.layoutsCreated) / scrolled, QStringLiteral("count")
    );
    out << valueResult(
      label + QStringLiteral("/nodes_created"), double(after.scene.nodesCreated - before.scene.nodesCreated),
      QStringLiteral("count")
    );
    out << valueResult(
      label + QStringLiteral("/nodes_recycled_per_frame"),
      double(after.scene.nodesRecycled - before.scene.nodesRecycled) / scrolled, QStringLiteral("count")
    );
    out << valueResult(
      label + QStringLiteral("/layouts_cached"), double(after.layoutsCached), QStringLiteral("count")
    );
    out << valueResult(
      label + QStringLiteral("/memory/rss_growth_scroll"), double(residentBytes() - rssBefore),
      QStringLiteral("bytes")
    );
    return out;
  }

private:
  enum class Phase { Idle, Loading, Scrolling };

  void runLoop() {
    QEventLoop loop;
    m_loop = &loop;
    loop.exec();
    m_loop = nullptr;
  }

  // Lets pending polish/sync/render finish and the frame pipeline drain.
  void settle() {
    QElapsedTimer t;
    t.start();
    m_phase = Phase::Idle;
    const qint64 frames = m_timer.frameCount();
    while (t.elapsed() < 2000 && m_timer.frameCount() < frames + 3)
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  }

  void advance() {
    switch (m_mode) {
    case Mode::Smooth:
    case Mode::Fling:
      m_y += m_step;
      if (m_y > m_range)
        m_y = m_range / 2; // wrap around instead of sticking at the end
      break;
    case Mode::Jumps:
      m_y = std::uniform_real_distribution<qreal>(0, m_range)(m_rng);
      break;
    case Mode::Cursor:
      m_y += m_step;
      if (m_y > m_range)
        m_y = m_range / 2;
      // The cursor stays a few rows below the top of the view, so every frame moves it and the numbers.
      m_editor.setCursorPosition(m_editor.document()->rope().lineStart(
        m_editor.displayMap().lineForRow(qsizetype(m_y / m_editor.metrics().lineHeight()) + 5)));
      break;
    }
    m_editor.setContentY(m_y);
  }

  void onFrame() {
    if (m_phase == Phase::Loading) {
      // The swap that first sees text may present a frame rendered before the text arrived, so the
      // first frame that certainly shows it is the next one.
      if (m_firstFrameNs < 0 && m_editor.lineCount() > 20) {
        if (m_textSeen)
          m_firstFrameNs = m_openClock.nsecsElapsed();
        m_textSeen = true;
      }
      return;
    }
    if (m_phase != Phase::Scrolling)
      return;
    if (++m_frameNo >= m_frames) {
      m_phase = Phase::Idle;
      if (m_loop)
        m_loop->quit();
      return;
    }
    advance();
  }

  QQuickView &m_view;
  CodeEditor &m_editor;
  int m_frames;
  FrameTimer m_timer;
  QEventLoop *m_loop = nullptr;
  Phase m_phase = Phase::Idle;
  Mode m_mode = Mode::Smooth;
  qreal m_step = 0, m_y = 0, m_range = 0;
  int m_frameNo = 0;
  std::mt19937_64 m_rng;
  qint64 m_firstFrameNs = -1;
  bool m_textSeen = false;
  qint64 m_rssAfterLoad = 0;
  QElapsedTimer m_openClock;
};

} // namespace

int main(int argc, char **argv) {
  QSurfaceFormat format = QSurfaceFormat::defaultFormat();
  format.setSwapInterval(0); // don't let vsync hide slow frames behind waiting
  QSurfaceFormat::setDefaultFormat(format);
  QGuiApplication app(argc, argv);

  QCommandLineParser parser;
  parser.addHelpOption();
  parser.addOption(
    {{QStringLiteral("o"), QStringLiteral("json")},
     QStringLiteral("Write results as JSON to <file>."),
     QStringLiteral("file")}
  );
  parser.addOption(
    {{QStringLiteral("n"), QStringLiteral("frames")},
     QStringLiteral("Frames per scroll scenario."),
     QStringLiteral("n"),
     QStringLiteral("600")}
  );
  parser.addOption({QStringLiteral("quick"), QStringLiteral("Small input and few frames (smoke test).")});
  parser.addOption({QStringLiteral("no-brackets"), QStringLiteral("Turn matching bracket highlighting off (the baseline).")});
  parser.addOption({QStringLiteral("no-guides"), QStringLiteral("Turn indent guides off (the baseline).")});
  parser.addOption({QStringLiteral("gutter"), QStringLiteral("Show line numbers, change bars and markers.")});
  parser.addOption({QStringLiteral("relative"), QStringLiteral("With --gutter: relative line numbers.")});
  parser.addOption({QStringLiteral("wrap"), QStringLiteral("Wrap at the viewport width.")});
  parser.addOption(
    {QStringLiteral("cursors"), QStringLiteral("Keep N cursors (one per line, spread through the file) while scrolling."),
     QStringLiteral("n"), QStringLiteral("0")}
  );
  parser.addOption({QStringLiteral("folds"), QStringLiteral("Fold 20,000 regions spread through the file.")});
  parser.addOption(
    {QStringLiteral("diagnostics"), QStringLiteral("Show N diagnostics (squiggles, icons, end-of-line messages)."),
     QStringLiteral("n"), QStringLiteral("0")}
  );
  parser.addOption({QStringLiteral("dense"), QStringLiteral("With --diagnostics: one per line around the middle of the file.")});
  parser.addOption(
    {{QStringLiteral("f"), QStringLiteral("filter")},
     QStringLiteral("Only scenarios whose name matches this regex."),
     QStringLiteral("regex")}
  );
  parser.process(app);
  const bool quick = parser.isSet(QStringLiteral("quick"));
  const int frames = quick ? 30 : parser.value(QStringLiteral("frames")).toInt();

  QList<Scenario> scenarios;
  if (quick) {
    const QString small =
      inputFile(QStringLiteral("qce_scroll_2MB.txt"), Shape::ManyShortLines, 2 * 1024 * 1024);
    scenarios << Scenario{QStringLiteral("short_2MB/smooth"), small, Mode::Smooth}
              << Scenario{QStringLiteral("short_2MB/jumps"), small, Mode::Jumps}
              << Scenario{QStringLiteral("short_2MB/cursor"), small, Mode::Cursor};
  } else {
    const QString small =
      inputFile(QStringLiteral("qce_scroll_10MB.txt"), Shape::ManyShortLines, 10 * 1024 * 1024);
    const QString big =
      inputFile(QStringLiteral("qce_core_100MB.txt"), Shape::ManyShortLines, 100 * 1024 * 1024);
    scenarios << Scenario{QStringLiteral("short_10MB/smooth"), small, Mode::Smooth}
              << Scenario{QStringLiteral("short_100MB/smooth"), big, Mode::Smooth}
              << Scenario{QStringLiteral("short_100MB/fling"), big, Mode::Fling}
              << Scenario{QStringLiteral("short_100MB/jumps"), big, Mode::Jumps}
              << Scenario{QStringLiteral("short_100MB/cursor"), big, Mode::Cursor};
  }

  QQuickView view;
  view.resize(1280, 800);
  auto *editor = new CodeEditor(view.contentItem());
  editor->setSize(QSizeF(1280, 800));
  editor->setCursorBlinkInterval(0); // blink repaints would add frames the scroll didn't ask for
  editor->setMatchBrackets(!parser.isSet(QStringLiteral("no-brackets")));
  editor->setShowIndentGuides(!parser.isSet(QStringLiteral("no-guides")));
  QString variant;
  qce::MarkerColumn *markers = nullptr;
  if (parser.isSet(QStringLiteral("gutter"))) {
    auto *numbers = new qce::LineNumberColumn(editor);
    if (parser.isSet(QStringLiteral("relative")))
      numbers->setMode(qce::LineNumberColumn::Relative);
    editor->addGutterColumn(numbers);
    editor->addGutterColumn(new qce::ChangeColumn(editor));
    markers = new qce::MarkerColumn(editor);
    editor->addGutterColumn(markers);
    if (parser.isSet(QStringLiteral("folds")))
      editor->addGutterColumn(new qce::FoldColumn(editor));
    variant += QStringLiteral("gutter/");
  }
  const bool folds = parser.isSet(QStringLiteral("folds"));
  const int diagnostics = parser.value(QStringLiteral("diagnostics")).toInt();
  const bool dense = parser.isSet(QStringLiteral("dense"));
  if (diagnostics > 0) {
    editor->addGutterColumn(new qce::DecorationColumn(editor));
    editor->setDiagnosticMessages(CodeEditor::EndOfLineMessages);
    variant += QStringLiteral("diagnostics%1%2/").arg(diagnostics).arg(dense ? QStringLiteral("dense") : QString());
  }
  const int cursors = parser.value(QStringLiteral("cursors")).toInt();
  if (cursors > 0)
    variant += QStringLiteral("cursors%1/").arg(cursors);
  if (folds)
    variant += QStringLiteral("folds/");
  if (parser.isSet(QStringLiteral("wrap"))) {
    editor->setWrapMode(CodeEditor::WrapAtViewport);
    variant += QStringLiteral("wrap/");
  }
  view.show();
  QElapsedTimer exposeWait;
  exposeWait.start();
  while (!view.isExposed() && exposeWait.elapsed() < 10000)
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  if (!view.isExposed()) {
    std::fprintf(stderr, "window was not exposed\n");
    return 1;
  }
  Bench bench(view, *editor, frames);

  QList<Result> results;
  QString currentPath;
  const QString filter = parser.value(QStringLiteral("filter"));
  for (const Scenario &s : scenarios) {
    if (!filter.isEmpty() && !QRegularExpression(filter).match(s.name).hasMatch())
      continue;
    const QString label = QStringLiteral("scroll/") + variant + s.name;
    if (s.path != currentPath) { // consecutive scenarios share an opened file
      results << bench.open(s.name.section(QLatin1Char('/'), 0, 0), s.path);
      // A hundred marks spread through the file, as a git or diagnostics provider might leave.
      if (markers)
        for (int i = 0; i < 100; ++i)
          markers->addMarker(editor->lineCount() / 100 * i, {{QStringLiteral("color"), QColor(Qt::red)}});
      if (cursors > 0) {
        const qce::Rope &rope = editor->document()->rope();
        const qsizetype lines = editor->lineCount(), step = qMax<qsizetype>(1, lines / cursors);
        editor->setCursorPosition(0);
        for (int i = 1; i < cursors && i * step < lines; ++i)
          editor->addSelection(rope.lineStart(i * step), rope.lineStart(i * step));
        editor->setContentY(0);
      }
      if (diagnostics > 0) {
        const qce::Rope &rope = editor->document()->rope();
        const qsizetype lines = editor->lineCount();
        const qsizetype first = dense ? qMax<qsizetype>(0, lines / 2 - diagnostics / 2) : 0;
        const qsizetype step = dense ? 1 : qMax<qsizetype>(1, lines / diagnostics);
        QList<qce::Diagnostic> list;
        list.reserve(diagnostics);
        for (int i = 0; i < diagnostics; ++i) {
          const qsizetype line = first + qsizetype(i) * step;
          if (line >= lines)
            break;
          const qsizetype length = rope.lineLength(line);
          qce::Diagnostic d;
          d.start = {line, qMin<qsizetype>(2, length)};
          d.end = {line, qMin<qsizetype>(6, length)};
          d.severity = 1 + i % 4;
          d.message = QStringLiteral("benchmark diagnostic %1").arg(i);
          list.append(d);
        }
        QElapsedTimer diagnosticsTimer;
        diagnosticsTimer.start();
        editor->setDiagnostics(list);
        const qint64 setNs = diagnosticsTimer.nsecsElapsed();
        std::fprintf(stderr, "pushed %d diagnostics in %lld ms\n", int(list.size()), setNs / 1000000);
        results << valueResult(QStringLiteral("scroll/") + variant + s.name + QStringLiteral("/diagnostics_set"), double(setNs), QStringLiteral("ns"));
        editor->setContentY(0);
      }
      if (folds) {
        QElapsedTimer foldTimer;
        foldTimer.start();
        const qsizetype lines = editor->lineCount();
        int made = 0;
        for (int i = 0; i < 20000; ++i) {
          const qsizetype from = lines / 20000 * i;
          const auto ranges = editor->foldRangesIn(from, from + 30);
          if (!ranges.isEmpty() && editor->fold(ranges.first().startLine))
            ++made;
        }
        std::fprintf(stderr, "folded %d regions in %lld ms\n", made, foldTimer.elapsed());
        results << valueResult(QStringLiteral("scroll/") + variant + s.name + QStringLiteral("/folds_made"), made, QStringLiteral("count"));
      }
      currentPath = s.path;
    }
    results << bench.scroll(label, s.mode);
  }

  int width = 4;
  for (const Result &r : results)
    width = std::max(width, int(r.name.size()));
  std::printf("%-*s  %14s  %14s  %14s  %s\n", width, "case", "median", "p95", "mean", "unit");
  for (const Result &r : results) {
    std::printf(
      "%-*s  %14.1f  %14.1f  %14.1f  %s\n", width, qPrintable(r.name), r.medianNs, r.p95Ns, r.meanNs,
      qPrintable(r.unit)
    );
  }
  if (parser.isSet(QStringLiteral("json"))) {
    QSaveFile out(parser.value(QStringLiteral("json")));
    if (!out.open(QIODevice::WriteOnly)) {
      std::fprintf(stderr, "cannot write %s\n", qPrintable(out.fileName()));
      return 1;
    }
    out.write(toJson(results, QStringLiteral("bench_scroll")));
    out.commit();
  }
  return 0;
}
