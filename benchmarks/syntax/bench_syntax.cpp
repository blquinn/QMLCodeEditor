// Syntax highlighting benchmarks (M6 exit criteria, SYNTAX-10).
//
//   bench_syntax [--quick] [--filter REGEX] [--json out.json]
//
// parse/full/<lang>           whole-document parse on a worker-style call, per document size
// parse/window/<size>         the window parse a huge document starts with
// parse/injections/markdown   Markdown with fenced code: block tree plus every embedded tree
// edit/gui_thread/<lang>      what the GUI thread pays for one keystroke: the edit, and spans for 60 lines
// edit/to_highlight/<lang>    keystroke until the reparsed spans are available (worker included)
// block/fill/<lang>           filling one 64-line block of spans from the tree
// memory/<lang>               heap bytes held by the tree (glibc mallinfo2), per million units of source
#include "bench.h"
#include "core/textdocument.h"
#include "syntax/languageregistry.h"
#include "syntax/parsejob.h"
#include "syntax/treesitterhighlighter.h"

#include <QtCore/QCommandLineParser>
#include <QtCore/QCoreApplication>
#include <QtCore/QElapsedTimer>
#include <QtCore/QEventLoop>
#include <QtCore/QRegularExpression>
#include <QtCore/QSaveFile>

#include <malloc.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>

using namespace qce;
using namespace qce::bench;
using namespace Qt::StringLiterals;

namespace {

// Bytes malloc currently hands out (arena plus mmapped blocks), for the memory cases. The parse runs
// on the calling thread, so its allocations are in this thread's arena.
qint64 heapBytes() {
  const struct mallinfo2 info = mallinfo2();
  return qint64(info.uordblks) + qint64(info.hblkhd);
}

// --- inputs ---------------------------------------------------------------------------------------------------

QString jsonSample(qint64 chars) {
  QString out = u"[\n"_s;
  for (int i = 0; out.size() < chars; ++i)
    out += u"  {\"id\": %1, \"name\": \"item %1\", \"tags\": [\"a\", \"b\"], \"ok\": true, \"v\": %1.5, \"n\": null},\n"_s.arg(i);
  return out + u"  {}\n]\n"_s;
}

QString javascriptSample(qint64 chars) {
  QString out;
  for (int i = 0; out.size() < chars; ++i)
    out += u"// helper %1\nfunction compute%1(x, y = %1) {\n  const total = [1, 2, 3].map((v) => v * x).filter(Boolean);\n"
           u"  return { name: \"item %1\", total, ok: y > 0 && /ab+c/.test(\"abbc\") };\n}\n\n"_s.arg(i);
  return out;
}

QString htmlSample(qint64 chars) {
  QString out = u"<!DOCTYPE html>\n<html><body>\n"_s;
  for (int i = 0; out.size() < chars; ++i)
    out += u"  <div class=\"row r%1\" id=\"d%1\"><p>Hello &amp; welcome %1</p><!-- note --></div>\n"
           u"  <script>var n%1 = %1; function f%1() { return n%1 * 2; }</script>\n"_s.arg(i);
  return out + u"</body></html>\n"_s;
}

QString xmlSample(qint64 chars) {
  QString out = u"<?xml version=\"1.0\"?>\n<root>\n"_s;
  for (int i = 0; out.size() < chars; ++i)
    out += u"  <item id=\"%1\" kind=\"a\"><name>Item %1</name><!-- c --><v>&lt;%1&gt;</v></item>\n"_s.arg(i);
  return out + u"</root>\n"_s;
}

QString markdownSample(qint64 chars) {
  QString out;
  for (int i = 0; out.size() < chars; ++i)
    out += u"## Section %1\n\nSome *emphasis*, **strong** text, `code` and a [link](https://example.com/%1).\n\n"
           u"```javascript\nfunction f%1(x) { return x * %1; } // fenced\n```\n\n- item one\n- item two\n\n"_s.arg(i);
  return out;
}

QString sampleFor(const QString &language, qint64 chars) {
  QString text;
  if (language == u"javascript"_s)
    text = javascriptSample(chars);
  else if (language == u"json"_s)
    text = jsonSample(chars);
  else if (language == u"html"_s)
    text = htmlSample(chars);
  else if (language == u"xml"_s)
    text = xmlSample(chars);
  else
    text = markdownSample(chars);
  if (text.size() > chars + 4096) { // cut at a line end so the text stays well-formed-ish
    const qsizetype nl = text.indexOf(u'\n', chars);
    text.truncate(nl < 0 ? chars : nl + 1);
  }
  return text;
}

Rope ropeOf(const QString &text) {
  RopeBuilder builder;
  for (qsizetype i = 0; i < text.size(); i += 65536)
    builder.append(QStringView(text).mid(i, 65536));
  return builder.finish();
}

const QStringList kLanguages = {u"javascript"_s, u"json"_s, u"html"_s, u"xml"_s, u"markdown"_s};
const QString kFile[] = {u"a.js"_s, u"a.json"_s, u"a.html"_s, u"a.xml"_s, u"a.md"_s};

QString fileNameFor(const QString &language) { return kFile[kLanguages.indexOf(language)]; }

ParseResult parseWhole(const TextSnapshot &snapshot, const QString &language, bool windowed, qsizetype from, qsizetype to) {
  ParseRequest request;
  request.snapshot = snapshot;
  request.language = LanguageRegistry::instance().compiled(language);
  request.cancel = std::make_shared<std::atomic_bool>(false);
  request.windowed = windowed;
  request.start = from;
  request.end = to;
  return runParse(std::move(request));
}

// Runs the event loop until the highlighter has no parse running or queued.
void waitIdle(TreeSitterHighlighter &h) {
  QElapsedTimer guard;
  guard.start();
  while (h.parsing() && guard.elapsed() < 60000)
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
}

// Runs the event loop until a parse result newer than `landedBefore` is installed and the worker is idle.
// Where a benchmark keystroke goes: right after the first digit at or below the middle line, which
// keeps the text well-formed (a longer number or identifier) so the reparse is a typical one.
qsizetype typingOffset(const Rope &rope) {
  qsizetype at = rope.lineStart(rope.lineCount() / 2);
  while (at < rope.length() && !rope.at(at).isDigit())
    ++at;
  return qMin(at + 1, rope.length());
}

void waitForNext(TreeSitterHighlighter &h, quint64 landedBefore) {
  QElapsedTimer guard;
  guard.start();
  while ((h.stats().landed <= landedBefore || h.parsing()) && guard.elapsed() < 60000)
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
}

} // namespace

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  QCommandLineParser parser;
  parser.addHelpOption();
  parser.addOption({u"quick"_s, u"Small inputs and few iterations (smoke test)."_s});
  parser.addOption({{u"f"_s, u"filter"_s}, u"Only run cases matching this regex."_s, u"regex"_s});
  parser.addOption({{u"o"_s, u"json"_s}, u"Write results as JSON."_s, u"file"_s});
  parser.process(app);
  const bool quick = parser.isSet(u"quick"_s);
  const qint64 fullChars = quick ? 200'000 : 4'000'000; // UTF-16 units per whole-document parse
  const int iterations = quick ? 2 : 5;
  const QRegularExpression filter(parser.value(u"filter"_s));
  auto wanted = [&](const QString &name) { return filter.pattern().isEmpty() || filter.match(name).hasMatch(); };

  Runner runner;
  QList<Result> extra;
  QList<Result> *sink = &extra;

  for (const QString &language : kLanguages) {
    // Whole-document parse, no injections (those have their own case).
    runner.add(
      u"parse/full/"_s + language + (quick ? u"/0.2M_units"_s : u"/4M_units"_s),
      [=](Context &ctx) {
        static QHash<QString, std::pair<QString, Rope>> cache;
        auto &entry = cache[language];
        if (entry.first.isEmpty()) {
          entry.first = sampleFor(language, fullChars);
          entry.second = ropeOf(entry.first);
        }
        const TextSnapshot snapshot(entry.second, 1);
        ParseRequest request;
        request.snapshot = snapshot;
        request.language = LanguageRegistry::instance().compiled(language);
        request.cancel = std::make_shared<std::atomic_bool>(false);
        ParseResult result = runParse(std::move(request));
        doNotOptimize(result.tree.get());
        ctx.setItems(entry.second.length() / 1'000'000 + 1); // per million units
      },
      iterations, 1
    );
  }

  runner.add(
    u"parse/injections/markdown"_s,
    [=](Context &ctx) {
      static const Rope rope = ropeOf(sampleFor(u"markdown"_s, quick ? 100'000 : 1'000'000));
      ParseRequest request;
      request.snapshot = TextSnapshot(rope, 1);
      request.language = LanguageRegistry::instance().compiled(u"markdown"_s);
      request.cancel = std::make_shared<std::atomic_bool>(false);
      request.injectionStart = 0;
      request.injectionEnd = rope.length();
      ParseResult result = runParse(std::move(request));
      doNotOptimize(result.layers.size());
      ctx.setItems(qint64(result.layers.size()));
    },
    iterations, 1
  );

  // The window parse a huge document starts with: 100 MB (50 M units) of C++, window in the middle.
  runner.add(
    quick ? u"parse/window/javascript/2M_units_of_6M"_s : u"parse/window/javascript/2M_units_of_50M"_s,
    [=](Context &ctx) {
      static const Rope rope = ropeOf(sampleFor(u"javascript"_s, quick ? 6'000'000 : 50'000'000));
      const qsizetype middle = rope.lineStart(rope.lineCount() / 2);
      const qsizetype size = 2'000'000;
      ParseResult result = parseWhole(TextSnapshot(rope, 1), u"javascript"_s, true, middle, middle + size);
      doNotOptimize(result.tree.get());
      ctx.setItems(1);
    },
    iterations, 1
  );

  for (const QString &language : kLanguages) {
    const qint64 docChars = quick ? 100'000 : 1'000'000;
    auto setup = [=](TextDocument &doc, TreeSitterHighlighter &h) {
      doc.setText(sampleFor(language, docChars));
      h.setFileName(fileNameFor(language));
      h.attach(&doc);
      waitForNext(h, 0);
    };

    runner.add(
      u"edit/gui_thread/"_s + language,
      [=](Context &ctx) {
        static QHash<QString, std::shared_ptr<std::pair<TextDocument, TreeSitterHighlighter>>> held;
        auto &pair = held[language];
        if (!pair) {
          pair = std::make_shared<std::pair<TextDocument, TreeSitterHighlighter>>();
          setup(pair->first, pair->second);
        }
        TextDocument &doc = pair->first;
        TreeSitterHighlighter &h = pair->second;
        const qsizetype at = typingOffset(doc.rope());
        const qsizetype line = doc.rope().lineAt(at);
        const int keystrokes = 50;
        for (int i = 0; i < keystrokes; ++i) {
          ctx.stopTimer();
          waitIdle(h); // let the previous reparse land: measure the edit alone
          ctx.startTimer();
          doc.insert(at, u"7"_s);
          doNotOptimize(h.highlightLines(doc.snapshot(), line, line + 59));
        }
        ctx.setItems(keystrokes);
      },
      quick ? 2 : 5, 1
    );

    runner.add(
      u"edit/to_highlight/"_s + language,
      [=](Context &ctx) {
        static QHash<QString, std::shared_ptr<std::pair<TextDocument, TreeSitterHighlighter>>> held;
        auto &pair = held[language];
        if (!pair) {
          pair = std::make_shared<std::pair<TextDocument, TreeSitterHighlighter>>();
          setup(pair->first, pair->second);
        }
        TextDocument &doc = pair->first;
        TreeSitterHighlighter &h = pair->second;
        const qsizetype at = typingOffset(doc.rope());
        const qsizetype line = doc.rope().lineAt(at);
        const int keystrokes = 20;
        for (int i = 0; i < keystrokes; ++i) {
          waitIdle(h);
          const quint64 landed = h.stats().landed;
          doc.insert(at, u"7"_s);
          waitForNext(h, landed);
          doNotOptimize(h.highlightLines(doc.snapshot(), line, line + 59));
        }
        // What the worker alone spent on the last reparse (tree-sitter, then injections).
        auto record = [&](const QString &name, qint64 ns) {
          sink->removeIf([&](const Result &r) { return r.name == name; }); // keep the last sample only
          sink->append(valueResult(name, double(ns), u"ns"_s));
        };
        record(u"edit/worker_reparse/"_s + language, h.stats().lastParseNs);
        record(u"edit/worker_injections/"_s + language, h.stats().lastInjectionNs);
        ctx.setItems(keystrokes);
      },
      quick ? 2 : 5, 1
    );

    runner.add(
      u"block/fill/"_s + language,
      [=](Context &ctx) {
        static QHash<QString, std::shared_ptr<std::pair<TextDocument, TreeSitterHighlighter>>> held;
        auto &pair = held[language];
        if (!pair) {
          pair = std::make_shared<std::pair<TextDocument, TreeSitterHighlighter>>();
          setup(pair->first, pair->second);
        }
        TextDocument &doc = pair->first;
        TreeSitterHighlighter &h = pair->second;
        const qsizetype lines = doc.rope().lineCount();
        const int blocks = 40;
        // Distinct blocks all over the document, so none is cached yet (the cache holds 512).
        for (int i = 0; i < blocks; ++i) {
          const qsizetype first = (lines / blocks) * i;
          doNotOptimize(h.highlightLines(doc.snapshot(), first - first % 64, first - first % 64 + 63));
        }
        ctx.setItems(blocks);
        // Drop them again for the next iteration with an edit at the top (shifts every later block out).
        ctx.stopTimer();
        doc.insert(0, u"\n"_s);
        waitIdle(h);
        ctx.startTimer();
      },
      quick ? 2 : 5, 1
    );
  }

  QList<Result> results = runner.run(parser.value(u"filter"_s));
  results += extra;

  // Memory: heap bytes the tree holds per million UTF-16 units of source (a parse that keeps its tree).
  for (const QString &language : kLanguages) {
    const QString name = u"memory/"_s + language;
    if (!wanted(name))
      continue;
    const Rope rope = ropeOf(sampleFor(language, fullChars));
    const qint64 before = heapBytes();
    ParseRequest request;
    request.snapshot = TextSnapshot(rope, 1);
    request.language = LanguageRegistry::instance().compiled(language);
    request.cancel = std::make_shared<std::atomic_bool>(false);
    ParseResult result = runParse(std::move(request));
    const qint64 held = heapBytes() - before;
    const double perMillion = double(held) / (double(rope.length()) / 1e6);
    results << valueResult(name + "/bytes_per_M_units", perMillion, u"bytes"_s);
    doNotOptimize(result.tree.get());
  }

  int width = 4;
  for (const Result &r : results)
    width = std::max(width, int(r.name.size()));
  std::printf("%-*s  %5s  %14s  %14s  %14s  %s\n", width, "case", "iters", "median", "p95", "median/item", "unit");
  for (const Result &r : results) {
    if (r.unit == u"ns"_s) {
      std::printf(
        "%-*s  %5d  %14s  %14s  %14s\n", width, qPrintable(r.name), r.iterations, qPrintable(formatDuration(r.medianNs)),
        qPrintable(formatDuration(r.p95Ns)), r.items > 1 ? qPrintable(formatDuration(r.medianNsPerItem())) : "-"
      );
    } else {
      std::printf("%-*s  %5d  %14.0f  %14s  %14s  %s\n", width, qPrintable(r.name), r.iterations, r.medianNs, "-", "-", qPrintable(r.unit));
    }
  }
  if (parser.isSet(u"json"_s)) {
    QSaveFile out(parser.value(u"json"_s));
    if (!out.open(QIODevice::WriteOnly)) {
      std::fprintf(stderr, "cannot write %s\n", qPrintable(out.fileName()));
      return 1;
    }
    out.write(toJson(results, u"bench_syntax"_s));
    out.commit();
  }
  return 0;
}
