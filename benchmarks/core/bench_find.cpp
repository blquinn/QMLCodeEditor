// Find/replace benchmarks (API-04): what a query costs on a 2M-line (~90 MB) document.
//
//   bench_find [--filter REGEX] [--json out.json] [--iterations N]
//
// "compile" is what the GUI thread does per keystroke before the worker starts; "worker" is the
// search or edit-building the worker thread runs on a snapshot (the GUI is not blocked, but this is
// how long the count or replace-all takes to arrive); "apply" is the GUI-thread cost of replace-all's
// single undo step; "navigate" is next/previous on a finished match list.
#include "bench.h"
#include "core/commands.h"
#include "core/textdocument.h"
#include "core/textsearch.h"

#include <QtCore/QCoreApplication>

#include <algorithm>
#include <limits>

using namespace qce;
using namespace qce::bench;
using namespace Qt::StringLiterals;

namespace {

constexpr qsizetype kLines = 2'000'000;

// "line N (has) some [text] in it"; every 1000th line also holds "needle".
const Rope &rope() {
  static const Rope r = [] {
    QString out;
    out.reserve(kLines * 40);
    for (qsizetype i = 0; i < kLines; ++i) {
      out += u"line "_s + QString::number(i) + u" (has) some [text] in it"_s;
      if (i % 1000 == 500)
        out += u" needle"_s;
      out += u'\n';
    }
    return Rope::fromString(out);
  }();
  return r;
}

void addSearch(Runner &runner, const char *name, const QString &text, search::Query query, int iterations = 5) {
  runner.add(
    name,
    [=](Context &ctx) {
      const search::Pattern pattern = search::compileQuery(text, query);
      const Rope &r = rope();
      ctx.setItems(r.length());
      ctx.startTimer();
      bool capped = false;
      const QList<Selection> all = search::findAll(r, pattern, 0, r.length(), 100'000, &capped);
      ctx.stopTimer();
      doNotOptimize(all.size());
    },
    iterations, 1
  );
}

} // namespace

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  Runner runner;

  runner.add(
    "compile/literal",
    [](Context &ctx) {
      ctx.setItems(1000);
      ctx.startTimer();
      for (int i = 0; i < 1000; ++i)
        doNotOptimize(search::compileQuery(u"needle%1"_s.arg(i), {false, false, false}).valid());
      ctx.stopTimer();
    },
    10, 1
  );
  runner.add(
    "compile/regex_wholeword",
    [](Context &ctx) {
      ctx.setItems(1000);
      ctx.startTimer();
      for (int i = 0; i < 1000; ++i)
        doNotOptimize(search::compileQuery(u"ne+dle\\d%1"_s.arg(i), {true, false, true}).valid());
      ctx.stopTimer();
    },
    10, 1
  );

  addSearch(runner, "worker/literal_2000_matches", u"needle"_s, {false, true, false});
  addSearch(runner, "worker/literal_icase", u"NEEDLE"_s, {false, false, false});
  addSearch(runner, "worker/literal_wholeword", u"needle"_s, {false, true, true});
  addSearch(runner, "worker/literal_no_match", u"zzzzzz"_s, {false, true, false});
  addSearch(runner, "worker/literal_100k_cap", u"line"_s, {false, true, false});
  addSearch(runner, "worker/regex_2000_matches", u"ne+dle"_s, {true, true, false}, 3);
  addSearch(runner, "worker/regex_no_match", u"zz+z"_s, {true, true, false}, 3);

  runner.add(
    "replace/build_literal_edits",
    [](Context &ctx) {
      const search::Pattern pattern = search::compileQuery(u"needle"_s, {false, true, false});
      const Rope &r = rope();
      ctx.setItems(r.length());
      ctx.startTimer();
      QList<commands::Replacement> edits;
      for (const Selection &m : search::findAll(r, pattern, 0, r.length(), std::numeric_limits<qsizetype>::max()))
        edits.append({m.start(), m.end(), u"pin"_s});
      ctx.stopTimer();
      doNotOptimize(edits.size());
    },
    5, 1
  );
  runner.add(
    "replace/build_regex_edits",
    [](Context &ctx) {
      const search::Pattern pattern = search::compileQuery(u"(ne+)dle"_s, {true, true, false});
      const Rope &r = rope();
      ctx.setItems(r.length());
      ctx.startTimer();
      QList<commands::Replacement> edits;
      search::forEachLineMatch(r, pattern.regex, 0, r.lineCount() - 1, [&](qsizetype base, const QRegularExpressionMatch &m) {
        edits.append({base + m.capturedStart(), base + m.capturedEnd(), search::expandReplacement(u"<$1>"_s, m)});
        return true;
      });
      ctx.stopTimer();
      doNotOptimize(edits.size());
    },
    3, 1
  );
  runner.add(
    "replace/apply_2000_edits_one_undo_step",
    [](Context &ctx) {
      TextDocument doc;
      doc.reset(rope());
      const search::Pattern pattern = search::compileQuery(u"needle"_s, {false, true, false});
      const QList<Selection> all = search::findAll(doc.rope(), pattern, 0, doc.length(), 100'000);
      ctx.setItems(all.size());
      ctx.startTimer();
      doc.beginEditGroup({{0, 0}});
      for (qsizetype i = all.size() - 1; i >= 0; --i)
        doc.replace(all[i].start(), all[i].end(), u"pin"_s);
      doc.endEditGroup({{0, 0}}, EditKind::Other);
      ctx.stopTimer();
      doNotOptimize(doc.length());
    },
    5, 1
  );

  runner.add(
    "navigate/next_in_2000_matches",
    [](Context &ctx) {
      QList<Selection> list;
      for (qsizetype i = 0; i < 2000; ++i)
        list.append({i * 1000, i * 1000 + 6});
      ctx.setItems(100'000);
      ctx.startTimer();
      qsizetype acc = 0;
      for (qsizetype from = 0; from < 2'000'000; from += 20) {
        const auto it = std::lower_bound(list.begin(), list.end(), from, [](const Selection &m, qsizetype o) {
          return m.start() < o;
        });
        acc += it == list.end() ? 0 : it->start();
      }
      ctx.stopTimer();
      doNotOptimize(acc);
    },
    10, 1
  );
  runner.add(
    "navigate/sync_find_next_literal",
    [](Context &ctx) {
      const search::Pattern pattern = search::compileQuery(u"needle"_s, {false, true, false});
      const Rope &r = rope();
      ctx.setItems(100);
      ctx.startTimer();
      qsizetype from = 0;
      for (int i = 0; i < 100; ++i) {
        const auto m = search::find(r, pattern, from);
        from = m ? m->start() : 0;
      }
      ctx.stopTimer();
      doNotOptimize(from);
    },
    5, 1
  );

  return runner.exec(app.arguments());
}
