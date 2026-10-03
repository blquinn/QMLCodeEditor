// Folding benchmarks (M7): working out ranges, folding and unfolding many regions at once, edits and
// row queries while folds exist.
//
//   bench_fold [--filter REGEX] [--json out.json] [--iterations N]
//
// The text is 2M lines of nested blocks (a function with an `if` inside), generated in memory.
#include "bench.h"
#include "core/displaymap.h"
#include "core/foldprovider.h"
#include "core/wrapmeasure.h"

#include <QtCore/QCoreApplication>

#include <random>

using namespace qce;
using namespace qce::bench;
using namespace Qt::StringLiterals;

namespace {

constexpr int kBlocks = 333'334; // 6 lines each: about 2M lines

QString blocksText(int blocks) {
  QString text;
  text.reserve(qsizetype(blocks) * 48);
  for (int i = 0; i < blocks; ++i) {
    text += u"function f"_s + QString::number(i) + u"(a, b) {\n"_s;
    text += u"  call(a, b);\n"_s;
    text += u"  if (a > b) {\n"_s;
    text += u"    other(a);\n"_s;
    text += u"  }\n"_s;
    text += u"}\n"_s;
  }
  return text;
}

WrapConfig wrapConfig() {
  WrapConfig c;
  c.mode = WrapMode::Column;
  c.column = 30;
  c.wordBreak = true;
  c.hangingIndent = false;
  c.measure = std::make_shared<GridWrapMeasure>(4);
  return c;
}

// A document of the shared text, with its provider's ranges.
struct Fixture {
  TextDocument doc;
  IndentFoldProvider provider;
  QList<FoldRange> ranges;
  Fixture() {
    doc.setText(blocksText(kBlocks));
    ranges = provider.foldRanges(doc.snapshot(), 0, doc.rope().lineCount() - 1);
  }
};

Fixture &fixture() {
  static Fixture f;
  return f;
}

// The first `count` ranges of every other block: folds with plenty of text between them.
QList<FoldRange> firstFolds(qsizetype count) { return fixture().ranges.first(count); }

// A map over the shared text (copied into its own document so edits don't leak between cases).
struct Mapped {
  TextDocument doc;
  DisplayMap map{&doc};
  explicit Mapped(bool wrap) {
    doc.reset(fixture().doc.rope());
    map.setBackgroundWrapping(false);
    if (wrap)
      map.setWrapConfig(wrapConfig());
  }
};

} // namespace

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  Runner runner;

  runner.add(
    "ranges/indent_all_2M_lines",
    [](Context &ctx) {
      Fixture &f = fixture();
      IndentFoldProvider provider; // no cache from earlier runs
      const QList<FoldRange> ranges = provider.foldRanges(f.doc.snapshot(), 0, f.doc.rope().lineCount() - 1);
      doNotOptimize(ranges.size());
      ctx.setItems(f.doc.rope().lineCount());
    },
    5, 1
  );

  runner.add(
    "ranges/indent_viewport_cold",
    [](Context &ctx) {
      Fixture &f = fixture();
      std::mt19937_64 rng(3);
      ctx.setItems(100);
      ctx.startTimer();
      for (int i = 0; i < 100; ++i) {
        IndentFoldProvider provider;
        const qsizetype first = qsizetype(rng() % 1'900'000);
        doNotOptimize(provider.foldRanges(f.doc.snapshot(), first, first + 120));
      }
      ctx.stopTimer();
    },
    10, 1
  );

  for (const bool wrap : {false, true}) {
    const QString suffix = wrap ? u"_wrap"_s : QString();
    runner.add(
      "fold/all_666k" + suffix,
      [wrap](Context &ctx) {
        ctx.stopTimer();
        Mapped m(wrap);
        ctx.startTimer();
        m.map.setFolds(fixture().ranges);
        ctx.stopTimer();
        doNotOptimize(m.map.rowCount());
        ctx.setItems(fixture().ranges.size());
      },
      5, 1
    );

    runner.add(
      "fold/unfold_all_666k" + suffix,
      [wrap](Context &ctx) {
        ctx.stopTimer();
        Mapped m(wrap);
        m.map.setFolds(fixture().ranges);
        ctx.startTimer();
        m.map.unfoldAll();
        ctx.stopTimer();
        doNotOptimize(m.map.rowCount());
      },
      5, 1
    );

    runner.add(
      "fold/toggle_one_of_100k" + suffix,
      [wrap](Context &ctx) {
        ctx.stopTimer();
        Mapped m(wrap);
        m.map.setFolds(firstFolds(100'000));
        std::mt19937_64 rng(5);
        ctx.setItems(200);
        ctx.startTimer();
        for (int i = 0; i < 100; ++i) {
          const FoldRange r = fixture().ranges[qsizetype(rng() % 100'000)];
          m.map.unfold(r.startLine);
          m.map.fold(r.startLine, r.endLine);
        }
        ctx.stopTimer();
        doNotOptimize(m.map.rowCount());
      },
      5, 1
    );

    runner.add(
      "fold/keystroke_with_100k_folds" + suffix,
      [wrap](Context &ctx) {
        ctx.stopTimer();
        Mapped m(wrap);
        m.map.setFolds(firstFolds(100'000));
        std::mt19937_64 rng(7);
        ctx.setItems(200);
        ctx.startTimer();
        // Typing inside one line, below the folds and above them: no line break, so no fold work.
        for (int i = 0; i < 200; ++i) {
          const qsizetype line = qsizetype(rng() % 1'000'000) * 2 + 1;
          const qsizetype at = m.doc.rope().lineStart(line) + 1;
          m.doc.insert(at, u"x"_s);
        }
        ctx.stopTimer();
      },
      5, 1
    );

    runner.add(
      "fold/enter_with_100k_folds" + suffix,
      [wrap](Context &ctx) {
        ctx.stopTimer();
        Mapped m(wrap);
        m.map.setFolds(firstFolds(100'000));
        std::mt19937_64 rng(9);
        ctx.setItems(50);
        ctx.startTimer();
        // A line break near the top moves every fold below it.
        for (int i = 0; i < 50; ++i) {
          const qsizetype line = qsizetype(rng() % 1000) * 6 + 1;
          m.doc.insert(m.doc.rope().lineEnd(line), u"\n"_s);
        }
        ctx.stopTimer();
        doNotOptimize(m.map.rowCount());
      },
      5, 1
    );

    runner.add(
      "fold/row_queries_100k_folds" + suffix,
      [wrap](Context &ctx) {
        ctx.stopTimer();
        Mapped m(wrap);
        m.map.setFolds(firstFolds(100'000));
        std::mt19937_64 rng(11);
        const qsizetype rows = m.map.rowCount();
        ctx.setItems(3000);
        ctx.startTimer();
        for (int i = 0; i < 1000; ++i) {
          const qsizetype row = qsizetype(rng() % quint64(rows));
          const DisplayRow r = m.map.rowAt(row);
          doNotOptimize(r.line);
          doNotOptimize(m.map.rowForPosition({r.line, 0}));
          doNotOptimize(m.map.firstRowOfLine(r.line));
        }
        ctx.stopTimer();
      },
      10, 1
    );
  }

  return runner.exec(app.arguments());
}
