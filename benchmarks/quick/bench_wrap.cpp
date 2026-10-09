// Soft-wrap benchmark (M4 exit criteria): toggling wrap and resizing on large files, a 5 MB single line, and
// typing inside it. Records time to the first frame after a change, per-frame sync/render cost and polish
// time while the rest of the document is wrapped on a worker, and how long that takes.
//
//   bench_wrap [--json out.json] [--frames N] [--quick] [--filter REGEX]
//
// Inputs are generated once into $QCE_BENCH_DIR (default: the system temp dir) and shared with bench_scroll.
// Run from a release build on a real display for meaningful numbers; --quick uses small files and few frames
// (the ctest smoke variant).
#include "bench.h"
#include "datagen.h"
#include "frametimer.h"
#include "quick/codeeditor.h"

#include <QtCore/QCommandLineParser>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QEventLoop>
#include <QtCore/QFileInfo>
#include <QtCore/QRegularExpression>
#include <QtCore/QSaveFile>
#include <QtCore/QTimer>
#include <QtGui/QGuiApplication>
#include <QtGui/QSurfaceFormat>
#include <QtQuick/QQuickView>
#include <QtQuick/QQuickWindow>

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace qce::bench;

namespace {

QString benchDir() {
  const QString dir = qEnvironmentVariable("QCE_BENCH_DIR", QDir::tempPath());
  QDir().mkpath(dir);
  return dir;
}

QString inputFile(const QString &name, Shape shape, qint64 bytes) {
  const QString path = benchDir() + QLatin1Char('/') + name;
  if (QFileInfo(path).size() < bytes * 99 / 100) {
    const QString err = writeSyntheticFile(path, shape, bytes, 1);
    if (!err.isEmpty())
      qFatal("cannot create benchmark input: %s", qPrintable(err));
  }
  return path;
}

void progress(const QString &what) { std::fprintf(stderr, "[bench_wrap] %s\n", qPrintable(what)); }

class Bench {
public:
  // `settleMs` bounds how long to wait for the frame pipeline to drain between scenarios.
  Bench(QQuickView &view, CodeEditor &editor, int settleMs)
      : m_editor(editor), m_timer(&view), m_settleMs(settleMs) {
    QObject::connect(&view, &QQuickWindow::frameSwapped, &view, [this] { onFrame(); }, Qt::QueuedConnection);
  }

  // Loads `path` with the given wrap mode already set, and reports time to the first frame with text and to
  // the end of the background wrap.
  QList<Result> open(const QString &label, const QString &path, CodeEditor::WrapMode mode, int column) {
    progress("open " + label);
    QList<Result> out;
    m_editor.setWrapMode(CodeEditor::NoWrap);
    m_editor.setText({});
    settle();
    m_editor.setWrapColumn(column);
    m_editor.setWrapMode(mode);
    m_editor.setContentY(0);
    m_firstFrameNs = -1;
    m_textSeen = false;
    m_phase = Phase::Loading;
    m_clock.start();
    QEventLoop loop;
    const auto done =
      QObject::connect(m_editor.document(), &qce::TextDocument::loadFinished, &loop, &QEventLoop::quit);
    QObject::connect(m_editor.document(), &qce::TextDocument::loadFailed, &loop, [&](const QString &e) {
      qFatal("load failed: %s", qPrintable(e));
    });
    m_editor.load(QUrl::fromLocalFile(path));
    loop.exec();
    QObject::disconnect(done);
    const qint64 loaded = m_clock.nsecsElapsed();
    QElapsedTimer wait;
    wait.start();
    while (m_firstFrameNs < 0 && wait.elapsed() < 10000) {
      m_editor.update(); // a document that finished loading before its first frame needs frames asked for
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    out << valueResult(
      label + QStringLiteral("/open/first_frame"), double(m_firstFrameNs), QStringLiteral("ns")
    );
    out << valueResult(label + QStringLiteral("/open/load_complete"), double(loaded), QStringLiteral("ns"));
    out << waitForWrap(label + QStringLiteral("/open"), m_clock);
    m_phase = Phase::Idle;
    return out;
  }

  // Changes the wrap setting while scrolling steadily, as a user would, and records what the change costs.
  QList<Result> toggle(const QString &label, CodeEditor::WrapMode mode, int column) {
    progress(label);
    QList<Result> out;
    settle();
    const qreal lh = m_editor.metrics().lineHeight();
    m_editor.setContentY(qMax<qreal>(0, m_editor.contentHeight() / 2));
    settle();
    m_timer.reset();
    m_editor.resetPolishStats();
    m_firstFrameNs = -1;
    m_framesSeen = 0;
    m_phase = Phase::Toggling;
    m_step = 2.5 * lh;
    QElapsedTimer clock;
    clock.start();
    m_clock = clock;
    m_editor.setWrapColumn(column);
    m_editor.setWrapMode(mode);
    // Scroll along (one step per frame) until the whole document is wrapped.
    QEventLoop loop;
    m_loop = &loop;
    QTimer::singleShot(120000, &loop, &QEventLoop::quit);
    QTimer idle;
    idle.setInterval(50);
    QObject::connect(&idle, &QTimer::timeout, &loop, [&] {
      if (!m_editor.wrapping() && m_framesSeen > 3)
        loop.quit();
      else
        m_editor.update(); // keep frames coming while only the worker is busy
    });
    idle.start();
    loop.exec();
    m_loop = nullptr;
    m_phase = Phase::Idle;
    const qint64 total = clock.nsecsElapsed();
    out << valueResult(label + QStringLiteral("/first_frame"), double(m_firstFrameNs), QStringLiteral("ns"));
    out << valueResult(label + QStringLiteral("/until_exact"), double(total), QStringLiteral("ns"));
    out += m_timer.results(label + QStringLiteral("/frame"));
    out << pollResults(label);
    return out;
  }

  // Changes the item's width every frame, sweeping between narrow and wide.
  QList<Result> resize(const QString &label, int frames) {
    progress(label);
    QList<Result> out;
    settle();
    m_timer.reset();
    m_editor.resetPolishStats();
    m_phase = Phase::Resizing;
    m_frameNo = 0;
    m_resizeFrames = frames;
    m_firstFrameNs = -1;
    m_clock.start();
    advanceResize();
    QEventLoop loop;
    m_loop = &loop;
    QTimer kick; // a frame that changed nothing would end the chain of frames that drive the sweep
    kick.setInterval(100);
    QObject::connect(&kick, &QTimer::timeout, &loop, [&] { m_editor.update(); });
    kick.start();
    loop.exec();
    m_loop = nullptr;
    const qint64 sweep = m_clock.nsecsElapsed();
    out << valueResult(label + QStringLiteral("/sweep"), double(sweep), QStringLiteral("ns"));
    out += m_timer.results(label + QStringLiteral("/frame"));
    out << pollResults(label);
    out << waitForWrap(label, m_clock);
    m_editor.setWidth(1280);
    return out;
  }

  // Inserts a character in the middle of the (single) line every frame.
  QList<Result> typing(const QString &label, int frames) {
    progress(label);
    QList<Result> out;
    settle();
    m_timer.reset();
    m_editor.resetPolishStats();
    m_editor.setCursorPosition(m_editor.document()->length() / 2);
    m_editor.ensureCursorVisible();
    settle();
    m_timer.reset();
    m_editor.resetPolishStats();
    m_phase = Phase::Typing;
    m_frameNo = 0;
    m_resizeFrames = frames;
    m_clock.start();
    m_editor.insert(QStringLiteral("x"));
    QEventLoop loop;
    m_loop = &loop;
    QTimer kick;
    kick.setInterval(100);
    QObject::connect(&kick, &QTimer::timeout, &loop, [&] { m_editor.update(); });
    kick.start();
    loop.exec();
    m_loop = nullptr;
    out += m_timer.results(label + QStringLiteral("/frame"));
    out << pollResults(label);
    return out;
  }

  // Moves the cursor to the end of the (single, unwrapped) line and reports the time until the view shows it.
  QList<Result> jumpToEnd(const QString &label) {
    progress(label);
    QList<Result> out;
    settle();
    m_editor.setContentX(0);
    m_editor.setCursorPosition(0);
    settle();
    m_timer.reset();
    m_editor.resetPolishStats();
    m_firstFrameNs = -1;
    m_framesSeen = 0;
    m_phase = Phase::Jumping;
    m_clock.start();
    m_editor.setCursorPosition(m_editor.document()->length());
    m_editor.ensureCursorVisible();
    QEventLoop loop;
    m_loop = &loop;
    QTimer kick;
    kick.setInterval(50);
    QObject::connect(&kick, &QTimer::timeout, &loop, [&] { m_editor.update(); });
    kick.start();
    QTimer::singleShot(20000, &loop, &QEventLoop::quit);
    loop.exec();
    m_loop = nullptr;
    out << valueResult(label + QStringLiteral("/first_frame"), double(m_firstFrameNs), QStringLiteral("ns"));
    out << pollResults(label);
    return out;
  }

  // Scrolls sideways by `step` pixels every frame, starting in the middle of the line.
  QList<Result> sideways(const QString &label, int frames, qreal step) {
    progress(label);
    QList<Result> out;
    settle();
    m_editor.setContentX(m_editor.contentWidth() / 2);
    settle();
    m_timer.reset();
    m_editor.resetPolishStats();
    m_phase = Phase::Sideways;
    m_frameNo = 0;
    m_resizeFrames = frames;
    m_step = step;
    m_clock.start();
    m_editor.setContentX(m_editor.contentX() + m_step);
    QEventLoop loop;
    m_loop = &loop;
    QTimer kick;
    kick.setInterval(100);
    QObject::connect(&kick, &QTimer::timeout, &loop, [&] { m_editor.update(); });
    kick.start();
    loop.exec();
    m_loop = nullptr;
    out += m_timer.results(label + QStringLiteral("/frame"));
    out << pollResults(label);
    return out;
  }

private:
  enum class Phase { Idle, Loading, Toggling, Resizing, Typing, Jumping, Sideways };

  QList<Result> pollResults(const QString &label) {
    const auto stats = m_editor.renderStats();
    return {
      valueResult(label + QStringLiteral("/polish_max"), double(stats.polishMaxNs), QStringLiteral("ns")),
      valueResult(
        label + QStringLiteral("/polish_mean"),
        double(stats.polishNs) / double(qMax<quint64>(1, stats.polishCalls)), QStringLiteral("ns")
      )
    };
  }

  QList<Result> waitForWrap(const QString &label, const QElapsedTimer &since) {
    QElapsedTimer wait;
    wait.start();
    while (m_editor.wrapping() && wait.elapsed() < 180000)
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return {
      valueResult(
        label + QStringLiteral("/wrap_complete"), double(since.nsecsElapsed()), QStringLiteral("ns")
      ),
      valueResult(
        label + QStringLiteral("/rows"), double(m_editor.displayMap().rowCount()), QStringLiteral("count")
      )
    };
  }

  void settle() {
    QElapsedTimer t;
    t.start();
    m_phase = Phase::Idle;
    const qint64 frames = m_timer.frameCount();
    while (t.elapsed() < m_settleMs && m_timer.frameCount() < frames + 3)
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  }

  void advanceResize() {
    // 400 .. 1280 px, one sweep over the run.
    const double t = double(m_frameNo) / double(qMax(1, m_resizeFrames));
    m_editor.setWidth(840 + 440 * std::cos(t * 4 * M_PI));
  }

  void onFrame() {
    switch (m_phase) {
    case Phase::Loading:
      if (m_firstFrameNs < 0 && (m_editor.lineCount() > 20 || m_editor.document()->length() > 4096)) {
        if (m_textSeen)
          m_firstFrameNs = m_clock.nsecsElapsed();
        m_textSeen = true;
      }
      return;
    case Phase::Toggling:
      ++m_framesSeen;
      if (m_framesSeen == 2) // the first frame may have been in flight before the change
        m_firstFrameNs = m_clock.nsecsElapsed();
      m_editor.setContentY(m_editor.contentY() + m_step);
      return;
    case Phase::Resizing:
      if (++m_frameNo >= m_resizeFrames) {
        m_phase = Phase::Idle;
        if (m_loop)
          m_loop->quit();
        return;
      }
      advanceResize();
      return;
    case Phase::Typing:
      if (++m_frameNo >= m_resizeFrames) {
        m_phase = Phase::Idle;
        if (m_loop)
          m_loop->quit();
        return;
      }
      m_editor.insert(QStringLiteral("x"));
      return;
    case Phase::Jumping:
      if (++m_framesSeen == 2) { // the first frame may have been in flight before the change
        m_firstFrameNs = m_clock.nsecsElapsed();
        m_phase = Phase::Idle;
        if (m_loop)
          m_loop->quit();
      }
      return;
    case Phase::Sideways:
      if (++m_frameNo >= m_resizeFrames) {
        m_phase = Phase::Idle;
        if (m_loop)
          m_loop->quit();
        return;
      }
      m_editor.setContentX(m_editor.contentX() + m_step);
      return;
    case Phase::Idle:
      return;
    }
  }

  CodeEditor &m_editor;
  qce::bench::FrameTimer m_timer;
  QEventLoop *m_loop = nullptr;
  Phase m_phase = Phase::Idle;
  QElapsedTimer m_clock;
  qint64 m_firstFrameNs = -1;
  bool m_textSeen = false;
  int m_frameNo = 0;
  int m_framesSeen = 0;
  int m_resizeFrames = 0;
  qreal m_step = 0;
  int m_settleMs;
};

} // namespace

int main(int argc, char **argv) {
  QSurfaceFormat format = QSurfaceFormat::defaultFormat();
  format.setSwapInterval(0);
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
     QStringLiteral("Frames per sweep or typing run."),
     QStringLiteral("n"),
     QStringLiteral("240")}
  );
  parser.addOption({QStringLiteral("quick"), QStringLiteral("Small inputs and few frames (smoke test).")});
  parser.addOption(
    {{QStringLiteral("f"), QStringLiteral("filter")},
     QStringLiteral("Only scenario groups whose name matches this regex."),
     QStringLiteral("regex")}
  );
  parser.process(app);
  const bool quick = parser.isSet(QStringLiteral("quick"));
  const int frames = quick ? 20 : parser.value(QStringLiteral("frames")).toInt();
  const QString filter = parser.value(QStringLiteral("filter"));
  auto wanted = [&](const QString &name) {
    return filter.isEmpty() || QRegularExpression(filter).match(name).hasMatch();
  };

  QQuickView view;
  view.resize(1280, 800);
  auto *editor = new CodeEditor(view.contentItem());
  editor->setSize(QSizeF(1280, 800));
  editor->setCursorBlinkInterval(0);
  view.show();
  QElapsedTimer exposeWait;
  exposeWait.start();
  while (!view.isExposed() && exposeWait.elapsed() < 10000)
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  if (!view.isExposed()) {
    std::fprintf(stderr, "window was not exposed\n");
    return 1;
  }
  Bench bench(view, *editor, quick ? 200 : 2000);
  QList<Result> results;

  // Many short lines: wrapping at 40 columns gives lines of 1-3 rows. The 100 MB file has about 2M lines.
  const QString lines =
    quick ? inputFile(QStringLiteral("qce_scroll_2MB.txt"), Shape::ManyShortLines, 2 * 1024 * 1024)
          : inputFile(QStringLiteral("qce_core_100MB.txt"), Shape::ManyShortLines, 100 * 1024 * 1024);
  const QString linesName = quick ? QStringLiteral("short_2MB") : QStringLiteral("short_100MB");
  if (wanted(QStringLiteral("toggle")) || wanted(QStringLiteral("resize"))) {
    results << bench.open(linesName, lines, CodeEditor::NoWrap, 80);
    if (wanted(QStringLiteral("toggle"))) {
      results << bench.toggle(
        QStringLiteral("wrap/") + linesName + QStringLiteral("/toggle_on_column40"), CodeEditor::WrapAtColumn,
        40
      );
      results << bench.toggle(
        QStringLiteral("wrap/") + linesName + QStringLiteral("/toggle_off"), CodeEditor::NoWrap, 40
      );
      results << bench.toggle(
        QStringLiteral("wrap/") + linesName + QStringLiteral("/toggle_on_viewport"),
        CodeEditor::WrapAtViewport, 40
      );
    }
    if (wanted(QStringLiteral("resize"))) {
      editor->setWrapMode(CodeEditor::WrapAtViewport);
      results << bench.resize(QStringLiteral("wrap/") + linesName + QStringLiteral("/resize_sweep"), frames);
    }
  }

  // One enormous line: 5 MB (1 MB when quick).
  if (wanted(QStringLiteral("giant"))) {
    const qint64 bytes = quick ? 1024 * 1024 : 5 * 1024 * 1024;
    const QString giant = inputFile(QStringLiteral("qce_wrap_giant.txt"), Shape::OneGiantLine, bytes);
    results << bench.open(QStringLiteral("giant_line"), giant, CodeEditor::WrapAtViewport, 80);
    results << bench.typing(QStringLiteral("wrap/giant_line/typing"), frames);
    results << bench.resize(QStringLiteral("wrap/giant_line/resize_sweep"), frames);
  }

  // The same line without wrap (PERF-01): only the visible stretch of it may be shaped.
  if (wanted(QStringLiteral("giant_nowrap"))) {
    const qint64 bytes = quick ? 1024 * 1024 : 5 * 1024 * 1024;
    const QString giant = inputFile(QStringLiteral("qce_wrap_giant.txt"), Shape::OneGiantLine, bytes);
    results << bench.open(QStringLiteral("giant_line_nowrap"), giant, CodeEditor::NoWrap, 80);
    results << bench.jumpToEnd(QStringLiteral("nowrap/giant_line/jump_to_end"));
    results << bench.sideways(QStringLiteral("nowrap/giant_line/sideways"), frames, 2000);
    results << bench.typing(QStringLiteral("nowrap/giant_line/typing"), frames);
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
    out.write(toJson(results, QStringLiteral("bench_wrap")));
    out.commit();
  }
  return 0;
}
