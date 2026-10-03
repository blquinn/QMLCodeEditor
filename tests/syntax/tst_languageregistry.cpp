#include "syntax/languageregistry.h"

#include <QtTest/QtTest>

using namespace qce;
using namespace Qt::StringLiterals;

class TstLanguageRegistry : public QObject {
  Q_OBJECT
private slots:
  void detectsByExtension_data() {
    QTest::addColumn<QString>("file");
    QTest::addColumn<QString>("id");
    QTest::newRow("cpp") << u"/a/b/main.CPP"_s << u"cpp"_s;
    QTest::newRow("header") << u"x.h"_s << u"c"_s;
    QTest::newRow("hpp") << u"x.hpp"_s << u"cpp"_s;
    QTest::newRow("json") << u"data.json"_s << u"json"_s;
    QTest::newRow("qml") << u"Main.qml"_s << u"qml"_s;
    QTest::newRow("js") << u"app.js"_s << u"javascript"_s;
    QTest::newRow("py") << u"tool.py"_s << u"python"_s;
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
    QCOMPARE(r.detect(u"script"_s, u"#!/usr/bin/env python3"_s)->id, u"python"_s);
    QCOMPARE(r.detect(u""_s, u"#!/usr/bin/python3.11"_s)->id, u"python"_s);
    QCOMPARE(r.detect(u""_s, u"#!/usr/bin/env -S node --flag"_s)->id, u"javascript"_s);
    QVERIFY(!r.detect(u"script"_s, u"#!/bin/sh"_s));
    QVERIFY(!r.detect(u"script"_s, u"no shebang"_s));
  }

  void findsAliases() {
    const auto &r = LanguageRegistry::instance();
    QCOMPARE(r.find(u"C++"_s)->id, u"cpp"_s);
    QCOMPARE(r.find(u"js"_s)->id, u"javascript"_s);
    QCOMPARE(r.find(u"Python"_s)->id, u"python"_s);
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
};

QTEST_GUILESS_MAIN(TstLanguageRegistry)
#include "tst_languageregistry.moc"
