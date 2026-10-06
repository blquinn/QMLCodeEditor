// Bracket matching benchmarks (API-06): what the editor pays on a cursor move, in the best and worst cases.
//
//   bench_brackets [--filter REGEX] [--json out.json] [--iterations N]
//
// The text is 2M numbered lines, generated in memory. The worst case is a bracket with no partner, which
// scans the whole allowed distance in either direction.
#include "bench.h"
#include "core/bracketmatch.h"
#include "core/rope.h"

#include <QtCore/QCoreApplication>

#include <random>

using namespace qce;
using namespace qce::bench;
using namespace Qt::StringLiterals;

namespace {

constexpr qsizetype kLines = 2'000'000;

const BracketPairs kPairs = {{u'(', u')'}, {u'[', u']'}, {u'{', u'}'}};

// "line N (has) some {text} in it" with no brackets left open.
QString text(bool withStrayBracket) {
  QString out;
  out.reserve(kLines * 40);
  if (withStrayBracket)
    out += u"{\n"_s;
  for (qsizetype i = 0; i < kLines; ++i)
    out += u"line "_s + QString::number(i) + u" (has) some [text] in it\n"_s;
  if (withStrayBracket)
    out += u"}\n"_s;
  return out;
}

const Rope &plainRope() {
  static const Rope rope = Rope::fromString(text(false));
  return rope;
}

const Rope &strayRope() {
  static const Rope rope = Rope::fromString(text(true));
  return rope;
}

} // namespace

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  Runner runner;

  runner.add(
    "match/adjacent_pair",
    [](Context &ctx) {
      const Rope &rope = plainRope();
      std::mt19937_64 rng(1);
      ctx.setItems(10000);
      ctx.startTimer();
      for (int i = 0; i < 10000; ++i) {
        const qsizetype line = qsizetype(rng() % quint64(kLines));
        // "line N (has)": the "(" follows "line ", the digits and a space (a stray "{" adds a line).
        const qsizetype open = rope.lineStart(line) + 5 + QString::number(line).size() + 1;
        doNotOptimize(findMatchingBracket(rope, open, kPairs));
      }
      ctx.stopTimer();
    },
    10, 1
  );

  runner.add(
    "match/near_cursor_no_bracket",
    [](Context &ctx) {
      const Rope &rope = plainRope();
      std::mt19937_64 rng(2);
      ctx.setItems(10000);
      ctx.startTimer();
      for (int i = 0; i < 10000; ++i)
        doNotOptimize(bracketNearCursor(rope, qsizetype(rng() % quint64(rope.length())), kPairs));
      ctx.stopTimer();
    },
    10, 1
  );

  // The opening "{" on the first line has its partner on the very last: further than the limit, so the
  // scan runs to the limit and gives up.
  runner.add(
    "match/worst_case_forward",
    [](Context &ctx) {
      const Rope &rope = strayRope();
      ctx.setItems(1);
      ctx.startTimer();
      doNotOptimize(findMatchingBracket(rope, 0, kPairs));
      ctx.stopTimer();
    },
    20, 2
  );

  runner.add(
    "match/worst_case_backward",
    [](Context &ctx) {
      const Rope &rope = strayRope();
      ctx.setItems(1);
      ctx.startTimer();
      doNotOptimize(findMatchingBracket(rope, rope.length() - 2, kPairs));
      ctx.stopTimer();
    },
    20, 2
  );

  return runner.exec(app.arguments());
}
