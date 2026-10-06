// Decoration and diagnostics benchmarks (M9): pushing 100k diagnostics, edits and queries while they exist,
// and soft wrap with inlay hints.
//
//   bench_decorations [--filter REGEX] [--json out.json] [--iterations N]
//
// The text is 2M numbered lines, generated in memory. Diagnostics sit on every 20th line (100k of them),
// as a language server with a lot to say about a big file might leave them.
#include "bench.h"
#include "core/decorationset.h"
#include "core/diagnostics.h"
#include "core/displaymap.h"
#include "core/wrapmeasure.h"

#include <QtCore/QCoreApplication>

#include <random>

using namespace qce;
using namespace qce::bench;
using namespace Qt::StringLiterals;

namespace {

constexpr qsizetype kLines = 2'000'000;
constexpr int kDiagnostics = 100'000;
constexpr int kInlayLayerForBench = -2;

QString numberedText(qsizetype lines) {
  QString text;
  text.reserve(lines * 34);
  for (qsizetype i = 0; i < lines; ++i)
    text += u"line "_s + QString::number(i) + u" has some text in it\n"_s;
  return text;
}

TextDocument &sharedDocument() {
  static TextDocument doc;
  static bool built = false;
  if (!built) {
    doc.setText(numberedText(kLines));
    built = true;
  }
  return doc;
}

QList<Diagnostic> diagnostics(const Rope &rope, int count, bool severities = true) {
  QList<Diagnostic> list;
  list.reserve(count);
  const qsizetype step = qMax<qsizetype>(1, rope.lineCount() / count);
  for (int i = 0; i < count; ++i) {
    const qsizetype line = qsizetype(i) * step;
    if (line >= rope.lineCount())
      break;
    Diagnostic d;
    d.start = {line, 2};
    d.end = {line, 6};
    d.severity = severities ? 1 + i % 4 : 1;
    d.message = u"benchmark diagnostic "_s + QString::number(i);
    list.append(d);
  }
  return list;
}

WrapConfig wrapConfig() {
  WrapConfig c;
  c.mode = WrapMode::Column;
  c.column = 30;
  c.wordBreak = true;
  c.measure = std::make_shared<GridWrapMeasure>(4);
  return c;
}

// A document of the shared text with its own decoration set and diagnostics (edits must not leak
// between cases).
struct Fixture {
  TextDocument doc;
  DecorationSet decorations{&doc};
  DiagnosticSet set{&doc, &decorations};
  Fixture() { doc.reset(sharedDocument().rope()); }
};

} // namespace

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  Runner runner;

  for (const bool messages : {false, true}) {
    const QString suffix = messages ? u"_with_messages"_s : QString();
    runner.add(
      "diagnostics/set_100k" + suffix,
      [messages](Context &ctx) {
        ctx.stopTimer();
        Fixture f;
        f.set.setEndOfLineMessages(messages);
        const QList<Diagnostic> list = diagnostics(f.doc.rope(), kDiagnostics);
        ctx.setItems(list.size());
        ctx.startTimer();
        f.set.setDiagnostics(list);
        ctx.stopTimer();
        doNotOptimize(f.set.count());
      },
      5, 1
    );
  }

  runner.add(
    "diagnostics/replace_100k",
    [](Context &ctx) {
      ctx.stopTimer();
      Fixture f;
      const QList<Diagnostic> list = diagnostics(f.doc.rope(), kDiagnostics);
      f.set.setDiagnostics(list);
      ctx.setItems(list.size());
      ctx.startTimer();
      f.set.setDiagnostics(list);
      ctx.stopTimer();
    },
    5, 1
  );

  runner.add(
    "diagnostics/clear_100k",
    [](Context &ctx) {
      ctx.stopTimer();
      Fixture f;
      f.set.setDiagnostics(diagnostics(f.doc.rope(), kDiagnostics));
      ctx.setItems(kDiagnostics);
      ctx.startTimer();
      f.set.clear();
      ctx.stopTimer();
    },
    5, 1
  );

  runner.add(
    "diagnostics/keystroke_with_100k",
    [](Context &ctx) {
      ctx.stopTimer();
      Fixture f;
      f.set.setDiagnostics(diagnostics(f.doc.rope(), kDiagnostics));
      std::mt19937_64 rng(7);
      ctx.setItems(200);
      ctx.startTimer();
      for (int i = 0; i < 200; ++i) {
        const qsizetype line = qsizetype(rng() % quint64(kLines));
        f.doc.insert(f.doc.rope().lineStart(line) + 1, u"x"_s);
      }
      ctx.stopTimer();
    },
    5, 1
  );

  runner.add(
    "diagnostics/line_break_with_100k",
    [](Context &ctx) {
      ctx.stopTimer();
      Fixture f;
      f.set.setDiagnostics(diagnostics(f.doc.rope(), kDiagnostics));
      std::mt19937_64 rng(9);
      ctx.setItems(50);
      ctx.startTimer();
      // A line break near the top moves every anchor below it.
      for (int i = 0; i < 50; ++i)
        f.doc.insert(f.doc.rope().lineEnd(qsizetype(rng() % 1000)), u"\n"_s);
      ctx.stopTimer();
    },
    5, 1
  );

  // The queries a frame and a hover make, against a set that is already there.
  struct Queried {
    Fixture f;
    Queried() { f.set.setDiagnostics(diagnostics(f.doc.rope(), kDiagnostics)); }
  };
  static std::unique_ptr<Queried> queried;
  auto queriedSet = []() -> Queried & {
    if (!queried)
      queried = std::make_unique<Queried>();
    return *queried;
  };

  runner.add(
    "diagnostics/viewport_query_100k",
    [queriedSet](Context &ctx) {
      Queried &q = queriedSet();
      std::mt19937_64 rng(3);
      ctx.setItems(1000);
      ctx.startTimer();
      constexpr quint32 kinds = decorationKindBit(DecorationKind::Squiggle) | decorationKindBit(DecorationKind::Underline) |
                                decorationKindBit(DecorationKind::Background);
      for (int i = 0; i < 1000; ++i) {
        const qsizetype first = qsizetype(rng() % quint64(kLines - 100));
        doNotOptimize(q.f.decorations.queryLines(first, first + 60, kinds));
      }
      ctx.stopTimer();
    },
    10, 1
  );

  runner.add(
    "diagnostics/at_offset_100k",
    [queriedSet](Context &ctx) {
      Queried &q = queriedSet();
      std::mt19937_64 rng(5);
      ctx.setItems(1000);
      ctx.startTimer();
      for (int i = 0; i < 1000; ++i)
        doNotOptimize(q.f.set.at(qsizetype(rng() % quint64(q.f.doc.length()))));
      ctx.stopTimer();
    },
    10, 1
  );

  runner.add(
    "diagnostics/next_100k",
    [queriedSet](Context &ctx) {
      Queried &q = queriedSet();
      std::mt19937_64 rng(11);
      ctx.setItems(1000);
      ctx.startTimer();
      for (int i = 0; i < 1000; ++i)
        doNotOptimize(q.f.set.next(qsizetype(rng() % quint64(q.f.doc.length())), true, ErrorSeverity));
      ctx.stopTimer();
    },
    10, 1
  );

  // Inlay hints and soft wrap: hints are looked up when a line is wrapped, so the cost is in the lines
  // that have them.
  struct Wrapped {
    TextDocument doc;
    DecorationSet decorations{&doc};
    DisplayMap map{&doc};
    Wrapped() {
      doc.reset(sharedDocument().rope());
      map.setBackgroundWrapping(false);
      map.setDecorations(&decorations);
      QObject::connect(&decorations, &DecorationSet::inlineLinesChanged, &map, &DisplayMap::rewrapLines);
      map.setWrapConfig(wrapConfig());
    }
    QList<DecorationSpec> hints(int count, qsizetype firstLine, qsizetype step) const {
      QList<DecorationSpec> specs;
      for (int i = 0; i < count; ++i) {
        DecorationSpec spec;
        spec.start = spec.end = doc.rope().lineStart(firstLine + qsizetype(i) * step) + 8;
        spec.kind = DecorationKind::InlineText;
        spec.text = u" : number "_s;
        spec.startGravity = Gravity::Left;
        specs.append(spec);
      }
      return specs;
    }
  };

  runner.add(
    "hints/set_2000_lines_with_wrap",
    [](Context &ctx) {
      ctx.stopTimer();
      Wrapped w;
      const QList<DecorationSpec> specs = w.hints(2000, 1'000'000, 1);
      doNotOptimize(w.map.rowCount());
      ctx.setItems(specs.size());
      ctx.startTimer();
      w.decorations.setLayer(kInlayLayerForBench, specs); // wraps the 2000 lines through the signal
      ctx.stopTimer();
      doNotOptimize(w.map.rowCountOfLine(1'000'000));
    },
    5, 1
  );

  runner.add(
    "hints/set_100k_lines_with_wrap",
    [](Context &ctx) {
      ctx.stopTimer();
      Wrapped w;
      const QList<DecorationSpec> specs = w.hints(100'000, 0, 20);
      ctx.setItems(specs.size());
      ctx.startTimer();
      w.decorations.setLayer(kInlayLayerForBench, specs); // 2000 lines at once, the rest marked
      ctx.stopTimer();
    },
    3, 1
  );

  runner.add(
    "hints/row_queries_with_100k_hints",
    [](Context &ctx) {
      ctx.stopTimer();
      Wrapped w;
      w.decorations.setLayer(kInlayLayerForBench, w.hints(100'000, 0, 20));
      std::mt19937_64 rng(13);
      const qsizetype rows = w.map.rowCount();
      ctx.setItems(1000);
      ctx.startTimer();
      for (int i = 0; i < 1000; ++i)
        doNotOptimize(w.map.rowAt(qsizetype(rng() % quint64(rows))).line);
      ctx.stopTimer();
    },
    5, 1
  );

  runner.add(
    "hints/keystroke_on_a_hinted_line",
    [](Context &ctx) {
      ctx.stopTimer();
      Wrapped w;
      w.decorations.setLayer(kInlayLayerForBench, w.hints(100'000, 0, 20));
      std::mt19937_64 rng(17);
      ctx.setItems(200);
      ctx.startTimer();
      for (int i = 0; i < 200; ++i) {
        const qsizetype line = qsizetype(rng() % 100'000) * 20;
        w.doc.insert(w.doc.rope().lineStart(line) + 1, u"x"_s);
      }
      ctx.stopTimer();
      doNotOptimize(w.map.rowCount());
    },
    5, 1
  );

  return runner.exec(app.arguments());
}
