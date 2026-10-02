// Text core benchmarks (M1 exit criteria): load, edit, lookup, snapshot, anchors, undo and save.
//
//   bench_core [--filter REGEX] [--json out.json] [--iterations N]
//
// The 100 MB input is generated once into $QCE_BENCH_DIR (default: the system temp dir) and reused.
#include "bench.h"
#include "core/anchorset.h"
#include "core/fileloader.h"
#include "core/filesaver.h"
#include "core/textdocument.h"
#include "datagen.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QEventLoop>
#include <QtCore/QFileInfo>

#include <random>

using namespace qce;
using namespace qce::bench;
using namespace Qt::StringLiterals;

namespace {

constexpr qint64 kFileBytes = 100 * 1024 * 1024;

QString benchDir() {
  const QString dir = qEnvironmentVariable("QCE_BENCH_DIR", QDir::tempPath());
  QDir().mkpath(dir);
  return dir;
}

QString inputPath() {
  const QString path = benchDir() + QStringLiteral("/qce_core_100MB.txt");
  if (QFileInfo(path).size() < kFileBytes * 99 / 100) {
    const QString err = writeSyntheticFile(path, Shape::ManyShortLines, kFileBytes, 1);
    if (!err.isEmpty())
      qFatal("cannot create benchmark input: %s", qPrintable(err));
  }
  return path;
}

// The 100 MB document, loaded once and shared by the cases that start from it.
const Rope &baseRope() {
  static const Rope rope = [] {
    LoadResult r = loadFile(inputPath());
    if (!r.ok)
      qFatal("load failed: %s", qPrintable(r.error));
    return r.text;
  }();
  return rope;
}

// Applies `count` random small edits: 60% inserts of 1-20 units, 40% deletes of up to 20 units.
Rope applyRandomEdits(Rope rope, int count, quint32 seed) {
  std::mt19937_64 rng(seed);
  static const QString words[] = {u"foo"_s,    u"bar_baz"_s,    u"\n"_s,        u"    "_s,
                                  u"(x, y)"_s, u"\U0001F600"_s, u"if (a) {\n"_s};
  for (int i = 0; i < count; ++i) {
    const qsizetype at = qsizetype(rng() % quint64(rope.length() + 1));
    if (rng() % 10 < 6)
      rope = rope.insert(at, words[rng() % std::size(words)]);
    else
      rope = rope.remove(at, at + qsizetype(rng() % 20));
  }
  return rope;
}

// The 100 MB document after 1M random edits: the state the lookup targets are measured on.
const Rope &editedRope() {
  static const Rope rope = applyRandomEdits(baseRope(), 1'000'000, 7);
  return rope;
}

} // namespace

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  Runner runner;

  runner.add(
    "load/first_snapshot_100MB",
    [](Context &ctx) {
      // time from start() until the first published snapshot, as the editor would see it
      const QString path = inputPath();
      QEventLoop loop;
      FileLoadJob job(path);
      qint64 firstLength = 0;
      QObject::connect(&job, &FileLoadJob::progress, &loop, [&](const Rope &text, qint64, qint64) {
        firstLength = text.length();
        loop.quit();
      });
      QObject::connect(&job, &FileLoadJob::finished, &loop, [&] { loop.quit(); });
      job.start();
      loop.exec();
      job.cancel();
      doNotOptimize(firstLength);
      ctx.setItems(1);
    },
    10, 2
  );

  runner.add(
    "load/full_100MB",
    [](Context &ctx) {
      const LoadResult r = loadFile(inputPath());
      doNotOptimize(r.text.length());
      ctx.setItems(1);
    },
    3, 1
  );

  runner.add(
    "edit/random_1M",
    [](Context &ctx) {
      const Rope r = applyRandomEdits(baseRope(), 1'000'000, 7);
      doNotOptimize(r.length());
      ctx.setItems(1'000'000);
    },
    3, 0
  );

  // Lookups run on the edited rope, in batches of 1000 so the clock overhead disappears.
  runner.add(
    "lookup/offset_to_pos",
    [](Context &ctx) {
      const Rope &r = editedRope();
      static std::mt19937_64 rng(11);
      QList<qsizetype> offsets;
      offsets.reserve(1000);
      for (int i = 0; i < 1000; ++i)
        offsets << qsizetype(rng() % quint64(r.length()));
      ctx.setItems(1000);
      ctx.startTimer();
      for (qsizetype o : offsets)
        doNotOptimize(r.positionAt(o));
      ctx.stopTimer();
    },
    300, 5
  );

  runner.add(
    "lookup/pos_to_offset",
    [](Context &ctx) {
      const Rope &r = editedRope();
      static std::mt19937_64 rng(12);
      QList<TextPosition> positions;
      positions.reserve(1000);
      for (int i = 0; i < 1000; ++i)
        positions << TextPosition{qsizetype(rng() % quint64(r.lineCount())), qsizetype(rng() % 40)};
      ctx.setItems(1000);
      ctx.startTimer();
      for (const TextPosition &p : positions)
        doNotOptimize(r.offsetAt(p));
      ctx.stopTimer();
    },
    300, 5
  );

  runner.add(
    "lookup/line_start",
    [](Context &ctx) {
      const Rope &r = editedRope();
      static std::mt19937_64 rng(13);
      QList<qsizetype> lines;
      for (int i = 0; i < 1000; ++i)
        lines << qsizetype(rng() % quint64(r.lineCount()));
      ctx.setItems(1000);
      ctx.startTimer();
      for (qsizetype l : lines)
        doNotOptimize(r.lineStart(l));
      ctx.stopTimer();
    },
    300, 5
  );

  runner.add(
    "snapshot/copy",
    [](Context &ctx) {
      const Rope &r = baseRope();
      ctx.setItems(100'000);
      for (int i = 0; i < 100'000; ++i) {
        Rope copy = r;
        doNotOptimize(copy);
      }
    },
    10, 1
  );

  runner.add(
    "anchors/edit_100k",
    [](Context &ctx) {
      AnchorSet set;
      for (int i = 0; i < 100'000; ++i)
        set.create(i * 10, i % 2 ? Gravity::Left : Gravity::Right);
      std::mt19937_64 rng(5);
      ctx.setItems(1000);
      ctx.startTimer();
      for (int i = 0; i < 1000; ++i) {
        const qsizetype at = qsizetype(rng() % 1'000'000);
        if (i % 2)
          set.applyEdit(at, at, at + 5);
        else
          set.applyEdit(at, at + 8, at);
      }
      ctx.stopTimer();
      doNotOptimize(set.size());
    },
    20, 2
  );

  runner.add(
    "undo/roundtrip_10k_edits",
    [](Context &ctx) {
      TextDocument doc;
      doc.reset(baseRope());
      std::mt19937_64 rng(3);
      ctx.stopTimer();
      for (int i = 0; i < 10'000; ++i) {
        const qsizetype at = qsizetype(rng() % quint64(doc.length() + 1));
        doc.replace(at, at + qsizetype(rng() % 5), u"edit");
      }
      ctx.startTimer();
      int undone = 0;
      while (doc.undo())
        ++undone;
      int redone = 0;
      while (doc.redo())
        ++redone;
      ctx.stopTimer();
      doNotOptimize(undone + redone);
      ctx.setItems(undone + redone);
    },
    5, 1
  );

  runner.add(
    "save/100MB",
    [](Context &ctx) {
      FileFormat format;
      const QString out = benchDir() + QStringLiteral("/qce_core_save.txt");
      QString error;
      if (!saveFile(baseRope(), out, format, &error))
        qFatal("save failed: %s", qPrintable(error));
      ctx.setItems(1);
    },
    3, 1
  );

  return runner.exec(app.arguments());
}
