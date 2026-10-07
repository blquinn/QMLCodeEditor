// Vim handler benchmarks (VIM-01..13): what one command costs on a 2M-line (~90 MB) document.
//
//   bench_vim [--filter REGEX] [--json out.json] [--iterations N]
//
// Every case feeds keys in vim notation through the handler, headless: key parsing, motion, operator,
// register and undo-group work, but no rendering. The 8 ms keystroke-to-frame target (ROADMAP) bounds
// what a command may cost; "typical" cases are the ones a user hits, the "worst" ones scan as far as
// the handler allows (a search that never matches, a paragraph motion in text without blank lines).
#include "bench.h"
#include "core/commands.h"
#include "core/cursorlayout.h"
#include "core/vim/vimhandler.h"

#include <QtCore/QCoreApplication>

#include <memory>
#include <random>

using namespace qce;
using namespace qce::bench;
using namespace Qt::StringLiterals;

namespace {

constexpr qsizetype kLines = 2'000'000;

// "line N (has) some [text] in it"; every 50th line is empty (paragraphs) and every 1000th holds "needle".
QString makeText(bool blanks) {
  QString out;
  out.reserve(kLines * 40);
  for (qsizetype i = 0; i < kLines; ++i) {
    if (blanks && i % 50 == 49) {
      out += u'\n';
      continue;
    }
    out += u"line "_s + QString::number(i) + u" (has) some [text] in it"_s;
    if (i % 1000 == 500)
      out += u" needle"_s;
    out += u'\n';
  }
  return out;
}

const Rope &rope(bool blanks) {
  static const Rope withBlanks = Rope::fromString(makeText(true));
  static const Rope without = Rope::fromString(makeText(false));
  return blanks ? withBlanks : without;
}

struct NoHost : InputHost {
  void copy() override {}
  void cut() override {}
  void paste() override {}
  void scrollRows(qsizetype) override {}
};

struct Bed {
  explicit Bed(bool blanks = true) {
    doc.reset(rope(blanks));
    sel.setSingle(0);
    map = std::make_unique<DisplayMap>(&doc);
    layout = std::make_unique<GridCursorLayout>(&doc, 4, 40, map.get());
    ctx = std::make_unique<EditContext>(EditContext{doc, sel, {}, map.get(), layout.get()});
    vim.activate(*ctx, host);
  }
  void keys(const QString &notation) { vim.feed(notation, *ctx, host); }
  // A random position a few characters into a line, away from the blank lines.
  void jump(std::mt19937_64 &rng) {
    const Rope &r = doc.rope();
    qsizetype line = qsizetype(rng() % quint64(kLines - 2));
    if (line % 50 == 49)
      ++line;
    sel.setSingle(r.lineStart(line) + 3);
  }

  TextDocument doc;
  SelectionSet sel{&doc};
  std::unique_ptr<DisplayMap> map;
  std::unique_ptr<GridCursorLayout> layout;
  std::unique_ptr<EditContext> ctx;
  VimInputHandler vim;
  NoHost host;
};

// A case that jumps to a random line and feeds `keys`, `n` times per iteration.
void addKeys(Runner &runner, const char *name, const QString &keys, int n = 500, int iterations = 10) {
  // Each case edits its own copy of the text (a Bed is built when the case first runs).
  auto holder = std::make_shared<std::unique_ptr<Bed>>();
  runner.add(
    name,
    [=](Context &ctx) {
      if (!*holder)
        *holder = std::make_unique<Bed>();
      Bed &bed = **holder;
      std::mt19937_64 rng(11);
      ctx.setItems(n);
      for (int i = 0; i < n; ++i) {
        bed.jump(rng);
        ctx.startTimer();
        bed.keys(keys);
        ctx.stopTimer();
      }
    },
    iterations, 1
  );
}

} // namespace

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  Runner runner;

  addKeys(runner, "motion/w", u"w"_s);
  addKeys(runner, "motion/5w", u"5w"_s);
  addKeys(runner, "motion/j", u"j"_s);
  addKeys(runner, "motion/20j", u"20j"_s);
  addKeys(runner, "motion/dollar", u"$"_s);
  addKeys(runner, "motion/f_char", u"fx"_s);
  addKeys(runner, "motion/percent", u"f(%"_s);
  addKeys(runner, "motion/paragraph", u"}"_s);
  addKeys(runner, "motion/G_gg", u"Ggg"_s, 100);
  addKeys(runner, "operator/dw", u"dw"_s);
  addKeys(runner, "operator/dd", u"dd"_s);
  addKeys(runner, "operator/x", u"x"_s);
  addKeys(runner, "operator/ciw", u"ciwX<Esc>"_s);
  addKeys(runner, "operator/yy_p", u"yyp"_s);
  addKeys(runner, "operator/di_paren", u"f(di("_s);
  addKeys(runner, "operator/dap", u"dap"_s);
  addKeys(runner, "insert/type_word", u"ihello world<Esc>"_s);
  addKeys(runner, "insert/o", u"onew line<Esc>"_s);
  addKeys(runner, "dot/x_dot", u"x."_s);
  addKeys(runner, "undo/dd_u", u"ddu"_s);
  addKeys(runner, "visual/viw_d", u"viwd"_s);
  addKeys(runner, "visual/V_10j_d", u"V10jd"_s, 200);
  addKeys(runner, "search/slash_typical", u"/needle<CR>"_s, 100);
  addKeys(runner, "search/star", u"*"_s, 100);

  // Worst cases: scan as far as the handler allows.
  runner.add(
    "worst/paragraph_without_blank_lines",
    [](Context &ctx) {
      static Bed bed(false);
      bed.sel.setSingle(0);
      ctx.setItems(1);
      ctx.startTimer();
      bed.keys(u"}"_s);
      ctx.stopTimer();
    },
    5, 1
  );
  runner.add(
    "worst/search_no_match",
    [](Context &ctx) {
      static Bed bed;
      bed.sel.setSingle(0);
      ctx.setItems(1);
      ctx.startTimer();
      bed.keys(u"/zzzzzz<CR>"_s);
      ctx.stopTimer();
    },
    3, 0
  );
  runner.add(
    "worst/search_regex_no_match",
    [](Context &ctx) {
      static Bed bed;
      bed.sel.setSingle(0);
      ctx.setItems(1);
      ctx.startTimer();
      bed.keys(u"/\\vz+q<CR>"_s);
      ctx.stopTimer();
    },
    3, 0
  );
  runner.add(
    "worst/percent_stray_bracket",
    [](Context &ctx) {
      static Bed bed(false);
      bed.doc.replace(0, 0, u"{\n");
      bed.sel.setSingle(0);
      ctx.setItems(1);
      ctx.startTimer();
      bed.keys(u"%"_s);
      ctx.stopTimer();
      bed.doc.remove(0, 2);
    },
    5, 1
  );

  // Bulk work.
  runner.add(
    "bulk/substitute_100k_lines",
    [](Context &ctx) {
      Bed bed;
      ctx.setItems(100000);
      ctx.startTimer();
      bed.keys(u":1,100000s/line/LINE/<CR>"_s);
      ctx.stopTimer();
    },
    3, 0
  );
  runner.add(
    "bulk/global_delete_20k_lines",
    [](Context &ctx) {
      Bed bed;
      ctx.setItems(20000);
      ctx.startTimer();
      bed.keys(u":1,1000000g/needle/d<CR>"_s); // 1000 matches in 1M lines
      ctx.stopTimer();
    },
    3, 0
  );
  runner.add(
    "bulk/block_1000_rows_delete",
    [](Context &ctx) {
      Bed bed;
      ctx.setItems(1000);
      ctx.startTimer();
      bed.keys(u"<C-v>999jld"_s);
      ctx.stopTimer();
    },
    5, 1
  );
  runner.add(
    "bulk/block_1000_rows_insert",
    [](Context &ctx) {
      Bed bed;
      ctx.setItems(1000);
      ctx.startTimer();
      bed.keys(u"<C-v>999jIxx<Esc>"_s);
      ctx.stopTimer();
    },
    5, 1
  );
  runner.add(
    "bulk/macro_100_repeats",
    [](Context &ctx) {
      Bed bed;
      bed.vim.setRegister(u'a', u"0wx+"_s);
      ctx.setItems(100);
      ctx.startTimer();
      bed.keys(u"100@a"_s);
      ctx.stopTimer();
    },
    5, 1
  );
  runner.add(
    "bulk/dot_100_repeats",
    [](Context &ctx) {
      Bed bed;
      bed.keys(u"dw"_s);
      ctx.setItems(100);
      ctx.startTimer();
      for (int i = 0; i < 100; ++i)
        bed.keys(u"j."_s);
      ctx.stopTimer();
    },
    5, 1
  );
  runner.add(
    "bulk/multicursor_1000_x",
    [](Context &ctx) {
      Bed bed;
      SelectionList list;
      for (int i = 0; i < 1000; ++i)
        list.append({bed.doc.rope().lineStart(i * 100) + 2, bed.doc.rope().lineStart(i * 100) + 2});
      bed.sel.set(list);
      ctx.setItems(1000);
      ctx.startTimer();
      bed.keys(u"x"_s);
      ctx.stopTimer();
    },
    5, 1
  );

  return runner.exec(app.arguments());
}
