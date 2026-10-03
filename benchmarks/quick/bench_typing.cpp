// Keystroke-to-frame benchmark (M3 exit criterion: under 8 ms on a 100 MB file): sends real key events
// to the editor in a shown window and measures the time from the event to the presented frame that
// shows its effect, plus how much of that is the editor handling the event on the GUI thread.
//
//   bench_typing [--json out.json] [--samples N] [--quick] [--filter REGEX]
//
// Inputs are generated once into $QCE_BENCH_DIR (default: the system temp dir) and reused. Run from a
// release build on a real display for meaningful numbers; --quick uses a small file and few samples
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
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>
#include <QtGui/QKeyEvent>
#include <QtGui/QSurfaceFormat>
#include <QtQuick/QQuickView>
#include <QtQuick/QQuickWindow>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <random>

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

// One keystroke's worth of work.
struct Action {
  QString name;
  // Performs the action on the editor (sending key events through the window) and returns.
  std::function<void(QQuickView &, CodeEditor &, std::mt19937_64 &)> run;
  // Puts the editor in the state the action expects, without being measured.
  std::function<void(QQuickView &, CodeEditor &, std::mt19937_64 &)> prepare;
};

void sendKey(QQuickView &view, Qt::Key key, const QString &text = {}, Qt::KeyboardModifiers mods = {}) {
  QKeyEvent press(QEvent::KeyPress, key, mods, text);
  QCoreApplication::sendEvent(&view, &press);
  QKeyEvent release(QEvent::KeyRelease, key, mods, text);
  QCoreApplication::sendEvent(&view, &release);
}

class Bench {
public:
  Bench(QQuickView &view, CodeEditor &editor)
      : m_view(view), m_editor(editor), m_frames(&view) {
    // Direct: runs on the render thread right after the item tree was synchronized.
    QObject::connect(&view, &QQuickWindow::afterSynchronizing, &view, [this] { m_synced = true; }, Qt::DirectConnection);
    QObject::connect(&view, &QQuickWindow::frameSwapped, &view, [this] { onSwap(); }, Qt::QueuedConnection);
  }

  QList<Result> open(const QString &label, const QString &path) {
    QEventLoop loop;
    auto done = QObject::connect(m_editor.document(), &qce::TextDocument::loadFinished, &loop, &QEventLoop::quit);
    QObject::connect(m_editor.document(), &qce::TextDocument::loadFailed, &loop, [](const QString &e) {
      qFatal("load failed: %s", qPrintable(e));
    });
    m_editor.load(QUrl::fromLocalFile(path));
    loop.exec();
    QObject::disconnect(done);
    settle();
    return {valueResult(label + QStringLiteral("/lines"), double(m_editor.lineCount()), QStringLiteral("count"))};
  }

  QList<Result> run(const QString &label, const Action &action, int samples) {
    std::mt19937_64 rng(7);
    QList<qint64> latency, handling;
    latency.reserve(samples);
    handling.reserve(samples);
    const int warmup = qMin(10, samples / 4);
    m_editor.forceActiveFocus();
    for (int i = 0; i < samples + warmup; ++i) {
      action.prepare(m_view, m_editor, rng);
      settle();
      if (i == warmup) {
        m_frames.reset();
        m_polishBefore = m_editor.renderStats();
      }
      m_synced = false;
      m_waiting = true;
      m_latency = -1;
      m_t0.start();
      action.run(m_view, m_editor, rng);
      const qint64 handled = m_t0.nsecsElapsed();
      waitForFrame();
      if (i >= warmup) {
        latency << (m_latency >= 0 ? m_latency : 2'000'000'000); // a lost frame counts as 2 s
        handling << handled;
      }
    }
    const auto after = m_editor.renderStats();
    QList<Result> out;
    out << summarize(label + QStringLiteral("/key_to_frame"), latency, 1);
    out << summarize(label + QStringLiteral("/key_handling"), handling, 1);
    out << valueResult(label + QStringLiteral("/key_to_frame_max"), double(*std::max_element(latency.begin(), latency.end())), QStringLiteral("ns"));
    out << valueResult(
      label + QStringLiteral("/polish_mean"),
      double(after.polishNs - m_polishBefore.polishNs) / double(qMax<quint64>(1, after.polishCalls - m_polishBefore.polishCalls)),
      QStringLiteral("ns")
    );
    out << m_frames.results(label + QStringLiteral("/frame"));
    return out;
  }

private:
  void onSwap() {
    if (!m_waiting || !m_synced)
      return;
    m_latency = m_t0.nsecsElapsed();
    m_waiting = false;
    if (m_loop)
      m_loop->quit();
  }

  void waitForFrame() {
    if (!m_waiting)
      return;
    QEventLoop loop;
    m_loop = &loop;
    QTimer::singleShot(2000, &loop, &QEventLoop::quit); // never hang on a lost frame
    loop.exec();
    m_loop = nullptr;
    m_waiting = false;
  }

  // Lets the frame pipeline drain between samples so each one starts from rest.
  void settle() {
    QElapsedTimer t;
    t.start();
    const qint64 frames = m_frames.frameCount();
    while (t.elapsed() < 40 && m_frames.frameCount() < frames + 2) // an idle editor renders no frames
      QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
  }

  QQuickView &m_view;
  CodeEditor &m_editor;
  FrameTimer m_frames;
  QEventLoop *m_loop = nullptr;
  std::atomic<bool> m_synced{false};
  bool m_waiting = false;
  QElapsedTimer m_t0;
  qint64 m_latency = -1;
  CodeEditor::RenderStats m_polishBefore;
};

} // namespace

int main(int argc, char **argv) {
  QSurfaceFormat format = QSurfaceFormat::defaultFormat();
  format.setSwapInterval(0); // don't let vsync hide slow frames behind waiting
  QSurfaceFormat::setDefaultFormat(format);
  QGuiApplication app(argc, argv);

  QCommandLineParser parser;
  parser.addHelpOption();
  parser.addOption({{QStringLiteral("o"), QStringLiteral("json")}, QStringLiteral("Write results as JSON to <file>."), QStringLiteral("file")});
  parser.addOption({{QStringLiteral("n"), QStringLiteral("samples")}, QStringLiteral("Keystrokes per scenario."), QStringLiteral("n"), QStringLiteral("200")});
  parser.addOption({QStringLiteral("quick"), QStringLiteral("Small input and few samples (smoke test).")});
  parser.addOption({{QStringLiteral("f"), QStringLiteral("filter")}, QStringLiteral("Only scenarios whose name matches this regex."), QStringLiteral("regex")});
  parser.process(app);
  const bool quick = parser.isSet(QStringLiteral("quick"));
  const int samples = quick ? 12 : parser.value(QStringLiteral("samples")).toInt();

  // Where the cursor goes before each keystroke: a random line in the middle half of the file, so
  // the edit lands in cold rope and layout cache and the view has to scroll there.
  auto placeCursor = [](QQuickView &, CodeEditor &editor, std::mt19937_64 &rng) {
    const qsizetype lines = editor.lineCount();
    const qsizetype line = std::uniform_int_distribution<qsizetype>(lines / 4, 3 * lines / 4)(rng);
    const qce::Rope &rope = editor.document()->rope();
    editor.setCursorPosition(rope.lineStart(line) + qMin<qsizetype>(10, rope.lineLength(line)));
    editor.ensureCursorVisible();
  };
  // Cursor stays where it is: the typical case of typing several characters in a row.
  auto keepCursor = [placeCursor](QQuickView &v, CodeEditor &e, std::mt19937_64 &r) {
    static bool placed = false;
    if (!placed) {
      placeCursor(v, e, r);
      placed = true;
    }
  };

  QList<Action> actions;
  actions << Action{QStringLiteral("type_char"), [](QQuickView &v, CodeEditor &, std::mt19937_64 &) { sendKey(v, Qt::Key_X, QStringLiteral("x")); }, placeCursor}
          << Action{QStringLiteral("type_run"), [](QQuickView &v, CodeEditor &, std::mt19937_64 &) { sendKey(v, Qt::Key_X, QStringLiteral("x")); }, keepCursor}
          << Action{QStringLiteral("enter"), [](QQuickView &v, CodeEditor &, std::mt19937_64 &) { sendKey(v, Qt::Key_Return); }, placeCursor}
          << Action{QStringLiteral("backspace"), [](QQuickView &v, CodeEditor &, std::mt19937_64 &) { sendKey(v, Qt::Key_Backspace); }, placeCursor}
          << Action{QStringLiteral("paste_line"), [](QQuickView &, CodeEditor &e, std::mt19937_64 &) { e.paste(); },
                    [placeCursor](QQuickView &v, CodeEditor &e, std::mt19937_64 &r) {
                      QGuiApplication::clipboard()->setText(QStringLiteral("    const auto value = compute(first, second, third);\n"));
                      placeCursor(v, e, r);
                    }}
          << Action{QStringLiteral("undo"), [](QQuickView &v, CodeEditor &, std::mt19937_64 &) { sendKey(v, Qt::Key_Z, QStringLiteral("z"), Qt::ControlModifier); },
                    [placeCursor](QQuickView &v, CodeEditor &e, std::mt19937_64 &r) {
                      placeCursor(v, e, r);
                      sendKey(v, Qt::Key_X, QStringLiteral("x")); // something to undo
                    }}
          << Action{QStringLiteral("arrow_down"), [](QQuickView &v, CodeEditor &, std::mt19937_64 &) { sendKey(v, Qt::Key_Down); }, keepCursor};

  // Multi-cursor (M8): a cursor at the start of each of N consecutive lines in the middle of the
  // file, typing one character at all of them. The cursors are placed once and stay between
  // samples, so every sample is a run of typing at N places (one undo step).
  for (const int cursors : {1000, 10000}) {
    auto place = [cursors](QQuickView &, CodeEditor &editor, std::mt19937_64 &) {
      if (editor.selectionCount() == cursors)
        return;
      const qce::Rope &rope = editor.document()->rope();
      const qsizetype first = editor.lineCount() / 2;
      editor.setCursorPosition(rope.lineStart(first));
      for (int i = 1; i < cursors; ++i)
        editor.addSelection(rope.lineStart(first + i), rope.lineStart(first + i));
      editor.ensureCursorVisible();
    };
    actions << Action{
      QStringLiteral("type_run_%1_cursors").arg(cursors),
      [](QQuickView &v, CodeEditor &, std::mt19937_64 &) { sendKey(v, Qt::Key_X, QStringLiteral("x")); }, place
    };
  }

  struct Input {
    QString label, path;
  };
  QList<Input> inputs;
  if (quick)
    inputs << Input{QStringLiteral("short_2MB"), inputFile(QStringLiteral("qce_scroll_2MB.txt"), Shape::ManyShortLines, 2 * 1024 * 1024)};
  else
    inputs << Input{QStringLiteral("short_100MB"), inputFile(QStringLiteral("qce_core_100MB.txt"), Shape::ManyShortLines, 100 * 1024 * 1024)};

  QQuickView view;
  view.resize(1280, 800);
  auto *editor = new CodeEditor(view.contentItem());
  editor->setSize(QSizeF(1280, 800));
  editor->setCursorBlinkInterval(0); // blink repaints would add frames the keystroke didn't ask for
  view.show();
  QElapsedTimer exposeWait;
  exposeWait.start();
  while (!view.isExposed() && exposeWait.elapsed() < 10000)
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  if (!view.isExposed()) {
    std::fprintf(stderr, "window was not exposed\n");
    return 1;
  }
  editor->forceActiveFocus();
  Bench bench(view, *editor);

  QList<Result> results;
  const QString filter = parser.value(QStringLiteral("filter"));
  for (const Input &input : inputs) {
    results << bench.open(input.label, input.path);
    for (const Action &action : actions) {
      const QString label = QStringLiteral("typing/") + input.label + QLatin1Char('/') + action.name;
      if (!filter.isEmpty() && !QRegularExpression(filter).match(label).hasMatch())
        continue;
      results << bench.run(label, action, samples);
    }
  }

  int width = 4;
  for (const Result &r : results)
    width = std::max(width, int(r.name.size()));
  std::printf("%-*s  %14s  %14s  %14s  %s\n", width, "case", "median", "p95", "mean", "unit");
  for (const Result &r : results)
    std::printf("%-*s  %14.1f  %14.1f  %14.1f  %s\n", width, qPrintable(r.name), r.medianNs, r.p95Ns, r.meanNs, qPrintable(r.unit));
  if (parser.isSet(QStringLiteral("json"))) {
    QSaveFile out(parser.value(QStringLiteral("json")));
    if (!out.open(QIODevice::WriteOnly)) {
      std::fprintf(stderr, "cannot write %s\n", qPrintable(out.fileName()));
      return 1;
    }
    out.write(toJson(results, QStringLiteral("bench_typing")));
    out.commit();
  }
  return 0;
}
