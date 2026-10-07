#include "core/highlighter.h"
#include "syntax/languageregistry.h"

#include <QtConcurrent/QtConcurrentRun>
#include <QtTest/QtTest>

using namespace qce;
using namespace Qt::StringLiterals;

namespace {
// A host language that borrows the JSON grammar: files "*.tl", `strings` captured in a host style.
LanguageInfo testLanguage() {
  LanguageInfo info;
  info.id = u"TestLang"_s; // lower-cased on registration
  info.name = u"Test language"_s;
  info.extensions = {u".TL"_s};
  info.fileNames = {u"Testfile"_s};
  info.interpreters = {u"testlang"_s};
  info.aliases = {u"TL"_s, u"tlang"_s};
  info.grammar = LanguageRegistry::instance().find(u"json"_s)->grammar;
  info.highlightSource = u"(string) @test.special\n(number) @number\n"_s;
  return info;
}
} // namespace

class TstLanguageRegistry : public QObject {
  Q_OBJECT
private slots:
  void detectsByExtension_data() {
    QTest::addColumn<QString>("file");
    QTest::addColumn<QString>("id");
    QTest::newRow("json") << u"data.json"_s << u"json"_s;
    QTest::newRow("js") << u"app.js"_s << u"javascript"_s;
    QTest::newRow("md") << u"notes.md"_s << u"markdown"_s;
    QTest::newRow("readme") << u"README"_s << u"markdown"_s;
    QTest::newRow("html") << u"index.HTML"_s << u"html"_s;
    QTest::newRow("xml") << u"pom.xml"_s << u"xml"_s;
    QTest::newRow("svg") << u"logo.svg"_s << u"xml"_s;
    QTest::newRow("ui") << u"form.ui"_s << u"xml"_s;
    QTest::newRow("unknown") << u"a.xyz"_s << QString();
  }
  void detectsByExtension() {
    QFETCH(QString, file);
    QFETCH(QString, id);
    const LanguageInfo *info = LanguageRegistry::instance().detect(file);
    QCOMPARE(info ? info->id : QString(), id);
  }

  void detectsByShebang() {
    const auto &r = LanguageRegistry::instance();
    QCOMPARE(r.detect(u"script"_s, u"#!/usr/bin/env node"_s)->id, u"javascript"_s);
    QCOMPARE(r.detect(u""_s, u"#!/usr/bin/nodejs"_s)->id, u"javascript"_s);
    QCOMPARE(r.detect(u""_s, u"#!/usr/bin/env -S node --flag"_s)->id, u"javascript"_s);
    QVERIFY(!r.detect(u"script"_s, u"#!/bin/sh"_s));
    QVERIFY(!r.detect(u"script"_s, u"no shebang"_s));
  }

  void findsAliases() {
    const auto &r = LanguageRegistry::instance();
    QCOMPARE(r.find(u"js"_s)->id, u"javascript"_s);
    QCOMPARE(r.find(u"HTM"_s)->id, u"html"_s);
    QVERIFY(!r.find(u"cobol"_s));
  }

  void grammarsLoadWithSupportedAbi() {
    for (const LanguageInfo &info : LanguageRegistry::instance().languages()) {
      const TSLanguage *language = info.grammar();
      QVERIFY2(language, qPrintable(info.id));
      const uint32_t abi = ts_language_abi_version(language);
      QVERIFY2(
        abi >= TREE_SITTER_MIN_COMPATIBLE_LANGUAGE_VERSION && abi <= TREE_SITTER_LANGUAGE_VERSION, qPrintable(info.id)
      );
    }
  }

  void queriesCompile() {
    for (const LanguageInfo &info : LanguageRegistry::instance().languages()) {
      auto compiled = LanguageRegistry::instance().compiled(info.id);
      QVERIFY(compiled);
      QVERIFY2(compiled->highlights, qPrintable(info.id));
      // Dropped patterns are tolerated but must stay a small minority; print them for review.
      for (const QString &w : compiled->warnings)
        qInfo().noquote() << info.id << w;
      QVERIFY2(compiled->warnings.size() <= 8, qPrintable(info.id));
    }
  }

  void badPatternsAreDropped() {
    QStringList warnings;
    TSQuery *q = compileQuery(
      LanguageRegistry::instance().find(u"json"_s)->grammar(),
      u"(string) @string\n(no_such_node) @x\n(number) @number\n"_s, &warnings
    );
    QVERIFY(q);
    QCOMPARE(ts_query_pattern_count(q), 2u);
    QCOMPARE(warnings.size(), 1);
    ts_query_delete(q);
  }

  // Host languages (API-12). These register into the process-wide registry, so they run last.
  void registeredLanguageIsFoundAndDetected() {
    const TokenStyle special = registerTokenStyle(u"test.special");
    QString error;
    QVERIFY2(LanguageRegistry::registerLanguage(testLanguage(), &error), qPrintable(error));
    const auto &r = LanguageRegistry::instance();
    QCOMPARE(r.find(u"testlang"_s)->id, u"testlang"_s);
    QCOMPARE(r.find(u"TL"_s)->id, u"testlang"_s);
    QCOMPARE(r.find(u"tlang"_s)->id, u"testlang"_s);
    QCOMPARE(r.detect(u"/some/dir/query.tl"_s)->id, u"testlang"_s);
    QCOMPARE(r.detect(u"Testfile"_s)->id, u"testlang"_s);
    QCOMPARE(r.detect(u"x"_s, u"#!/usr/bin/env testlang"_s)->id, u"testlang"_s);
    QCOMPARE(r.detect(u"data.json"_s)->id, u"json"_s); // built-ins still found
    

    const auto compiled = r.compiled(u"tlang"_s);
    QVERIFY(compiled);
    QVERIFY(compiled->highlights);
    QCOMPARE(compiled->info->id, u"testlang"_s);
    QVERIFY(compiled->warnings.isEmpty());
    QCOMPARE(compiled->highlightInfo.captureStyles.size(), size_t(2));
    QCOMPARE(compiled->highlightInfo.captureStyles[0], std::optional(special)); // @test.special
    QCOMPARE(compiled->highlightInfo.captureStyles[1], std::optional(TokenStyle::Number));
    QCOMPARE(r.compiled(u"testlang"_s).get(), compiled.get()); // cached
  }

  void queryFilesAndSourceAreConcatenated() {
    QTemporaryFile file;
    QVERIFY(file.open());
    file.write("(true) @constant\n");
    file.flush();
    LanguageInfo info = testLanguage();
    info.id = u"filequeries"_s;
    info.extensions = {u"fq"_s};
    info.highlightQueries = {file.fileName()};
    QVERIFY(LanguageRegistry::registerLanguage(info));
    const auto compiled = LanguageRegistry::instance().compiled(u"filequeries"_s);
    QVERIFY(compiled && compiled->highlights);
    QCOMPARE(ts_query_pattern_count(compiled->highlights), 3u); // the file's pattern, then the two inline
  }

  void reRegisteringReplacesAndDropsTheCache() {
    const auto before = LanguageRegistry::instance().compiled(u"testlang"_s);
    QVERIFY(before);
    const qsizetype count = qsizetype(LanguageRegistry::instance().languages().size());
    LanguageInfo info = testLanguage();
    info.highlightSource = u"(string) @string\n"_s;
    info.extensions = {u"tl2"_s};
    QVERIFY(LanguageRegistry::registerLanguage(info));
    const auto &r = LanguageRegistry::instance();
    QCOMPARE(qsizetype(r.languages().size()), count); // replaced, not added
    const auto after = r.compiled(u"testlang"_s);
    QVERIFY(after.get() != before.get());
    QCOMPARE(ts_query_pattern_count(after->highlights), 1u);
    QCOMPARE(ts_query_pattern_count(before->highlights), 2u); // the old one stays valid for whoever holds it
    QVERIFY(!r.detect(u"a.tl"_s));
    QCOMPARE(r.detect(u"a.tl2"_s)->id, u"testlang"_s);
  }

  void builtinCanBeOverriddenButNotRemoved() {
    const LanguageInfo original = *LanguageRegistry::instance().find(u"json"_s);
    LanguageInfo mine = original;
    mine.name = u"My JSON"_s;
    mine.extensions = {u"json"_s, u"myjson"_s};
    QVERIFY(LanguageRegistry::registerLanguage(mine));
    const auto &r = LanguageRegistry::instance();
    QCOMPARE(r.find(u"json"_s)->name, u"My JSON"_s);
    QCOMPARE(r.detect(u"a.myjson"_s)->id, u"json"_s);
    QVERIFY(r.compiled(u"json"_s)->highlights);
    QVERIFY(LanguageRegistry::registerLanguage(original)); // put the built-in back for the tests after
    QCOMPARE(r.find(u"json"_s)->name, original.name);
    QVERIFY(!r.detect(u"a.myjson"_s));
  }

  void invalidLanguagesAreRejected() {
    QString error;
    LanguageInfo none = testLanguage();
    none.grammar = nullptr;
    QVERIFY(!LanguageRegistry::registerLanguage(none, &error));
    QVERIFY(error.contains(u"grammar"_s));
    LanguageInfo nullGrammar = testLanguage();
    nullGrammar.grammar = [] { return static_cast<const TSLanguage *>(nullptr); };
    QVERIFY(!LanguageRegistry::registerLanguage(nullGrammar, &error));
    LanguageInfo noId = testLanguage();
    noId.id = u" "_s;
    QVERIFY(!LanguageRegistry::registerLanguage(noId, &error));
    QVERIFY(error.contains(u"id"_s));
  }

  void brokenQueriesAreDroppedNotFatal() {
    LanguageInfo info = testLanguage();
    info.id = u"brokenq"_s;
    info.extensions = {u"bq"_s};
    info.highlightSource = u"(string) @string\n(no_such_node) @x\n"_s;
    QVERIFY(LanguageRegistry::registerLanguage(info));
    const auto compiled = LanguageRegistry::instance().compiled(u"brokenq"_s);
    QVERIFY(compiled && compiled->highlights);
    QCOMPARE(compiled->warnings.size(), 1);
  }

  void compiledIsThreadSafe() {
    for (int round = 0; round < 3; ++round) {
      LanguageInfo info = testLanguage();
      info.id = u"threaded%1"_s.arg(round); // fresh each round: not compiled yet, so the workers race to build it
      info.extensions = {};
      QVERIFY(LanguageRegistry::registerLanguage(info));
      QList<QFuture<const CompiledLanguage *>> jobs;
      for (int i = 0; i < 8; ++i)
        jobs.append(QtConcurrent::run([id = info.id] {
          const auto &r = LanguageRegistry::instance();
          const auto compiled = r.compiled(id);
          return r.find(id) && compiled ? compiled.get() : nullptr;
        }));
      const CompiledLanguage *first = jobs.first().result();
      QVERIFY(first);
      for (auto &job : jobs)
        QCOMPARE(job.result(), first); // one compile, shared
    }
  }
};

QTEST_GUILESS_MAIN(TstLanguageRegistry)
#include "tst_languageregistry.moc"
