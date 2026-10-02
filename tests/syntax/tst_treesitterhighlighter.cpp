#include "syntax/treesitterhighlighter.h"

#include <QtTest/QtTest>

using namespace qce;
using namespace Qt::StringLiterals;

namespace {

TokenStyle styleAt(TreeSitterHighlighter &h, TextDocument &doc, qsizetype line, qsizetype column) {
  const auto lines = h.highlightLines(doc.snapshot(), line, line);
  for (const HighlightSpan &s : lines.first())
    if (column >= s.start && column < s.start + s.length)
      return s.style;
  return TokenStyle::Default;
}

// First occurrence of `needle` in the document as (line, column).
std::pair<qsizetype, qsizetype> find(TextDocument &doc, const QString &needle, qsizetype from = 0) {
  const QString text = doc.snapshot().toString();
  const qsizetype at = text.indexOf(needle, from);
  Q_ASSERT(at >= 0);
  const TextPosition p = doc.rope().positionAt(at);
  return {p.line, p.column};
}

} // namespace

class TstTreeSitterHighlighter : public QObject {
  Q_OBJECT

  static void settle(TreeSitterHighlighter &h) {
    QTRY_VERIFY_WITH_TIMEOUT(h.stats().landed >= 1 && !h.parsing(), 20000);
  }

  void expect(TreeSitterHighlighter &h, TextDocument &doc, const QString &needle, TokenStyle style, qsizetype skip = 0) {
    const auto [line, column] = find(doc, needle, skip);
    const TokenStyle actual = styleAt(h, doc, line, column);
    QVERIFY2(
      actual == style,
      qPrintable(u"'%1' at %2:%3 is %4, expected %5"_s.arg(needle).arg(line).arg(column)
                   .arg(tokenStyleName(actual)).arg(tokenStyleName(style)))
    );
  }

private slots:
  void cpp() {
    TextDocument doc;
    doc.setText(u"// note\n#include <vector>\nint main() {\n  return 42;\n}\nconst char *s = \"hi\";\n"_s);
    TreeSitterHighlighter h;
    h.setFileName(u"main.cpp"_s);
    h.attach(&doc);
    QCOMPARE(h.detectedLanguage(), u"cpp"_s);
    settle(h);
    expect(h, doc, u"// note"_s, TokenStyle::Comment);
    expect(h, doc, u"int"_s, TokenStyle::Type);
    expect(h, doc, u"return"_s, TokenStyle::Keyword);
    expect(h, doc, u"42"_s, TokenStyle::Number);
    expect(h, doc, u"\"hi\""_s, TokenStyle::String);
    expect(h, doc, u"main"_s, TokenStyle::Function);
  }

  void json() {
    TextDocument doc;
    doc.setText(u"{\"name\": \"x\", \"n\": 12, \"ok\": true}\n"_s);
    TreeSitterHighlighter h;
    h.setFileName(u"a.json"_s);
    h.attach(&doc);
    settle(h);
    expect(h, doc, u"\"name\""_s, TokenStyle::Property);
    expect(h, doc, u"\"x\""_s, TokenStyle::String);
    expect(h, doc, u"12"_s, TokenStyle::Number);
    expect(h, doc, u"true"_s, TokenStyle::Constant);
  }

  void qml() {
    TextDocument doc;
    doc.setText(u"import QtQuick 2.0\nItem {\n  width: 10\n  // c\n  onClicked: console.log(\"x\")\n}\n"_s);
    TreeSitterHighlighter h;
    h.setFileName(u"Main.qml"_s);
    h.attach(&doc);
    settle(h);
    expect(h, doc, u"import"_s, TokenStyle::Keyword);
    expect(h, doc, u"width"_s, TokenStyle::Property);
    expect(h, doc, u"10"_s, TokenStyle::Number);
    expect(h, doc, u"// c"_s, TokenStyle::Comment);
    expect(h, doc, u"\"x\""_s, TokenStyle::String);
  }

  void javascript() {
    TextDocument doc;
    doc.setText(u"function f(a) { return a + 1; }\n"_s);
    TreeSitterHighlighter h;
    h.setFileName(u"x.js"_s);
    h.attach(&doc);
    settle(h);
    expect(h, doc, u"function"_s, TokenStyle::Keyword);
    expect(h, doc, u"return"_s, TokenStyle::Keyword);
    expect(h, doc, u"1"_s, TokenStyle::Number);
  }

  void functionsInQml() {
    TextDocument doc;
    doc.setText(u"Item {\n  function f(a) { return a + 1 }\n}\n"_s);
    TreeSitterHighlighter h;
    h.setFileName(u"x.qml"_s);
    h.attach(&doc);
    settle(h);
    expect(h, doc, u"function"_s, TokenStyle::Keyword);
    expect(h, doc, u"return"_s, TokenStyle::Keyword);
    expect(h, doc, u"1"_s, TokenStyle::Number);
  }

  void python() {
    TextDocument doc;
    doc.setText(u"def f(x):\n    return \"s\"  # c\n"_s);
    TreeSitterHighlighter h;
    h.attach(&doc);
    h.setLanguage(u"python"_s);
    settle(h);
    expect(h, doc, u"def"_s, TokenStyle::Keyword);
    expect(h, doc, u"f(x)"_s, TokenStyle::Function);
    expect(h, doc, u"\"s\""_s, TokenStyle::String);
    expect(h, doc, u"# c"_s, TokenStyle::Comment);
  }

  void markdownWithInjections() {
    TextDocument doc;
    doc.setText(u"# Title\n\nSome *em* and `code` here\n\n```cpp\nint x = 1;\n```\n"_s);
    TreeSitterHighlighter h;
    h.setFileName(u"notes.md"_s);
    h.attach(&doc);
    settle(h);
    expect(h, doc, u"Title"_s, TokenStyle::Heading);
    expect(h, doc, u"em"_s, TokenStyle::Emphasis);
    expect(h, doc, u"code"_s, TokenStyle::Code);
    expect(h, doc, u"int"_s, TokenStyle::Type); // C++ inside the fence
    expect(h, doc, u"1;"_s, TokenStyle::Number);
  }

  void shebangPicksLanguage() {
    TextDocument doc;
    doc.setText(u"#!/usr/bin/env python3\nx = 1\n"_s);
    TreeSitterHighlighter h;
    h.attach(&doc);
    QCOMPARE(h.detectedLanguage(), u"python"_s);
  }

  void unknownLanguageIsPlain() {
    TextDocument doc;
    doc.setText(u"int a;\n"_s);
    TreeSitterHighlighter h;
    h.setFileName(u"data.xyz"_s);
    h.attach(&doc);
    QVERIFY(h.detectedLanguage().isEmpty());
    QVERIFY(h.highlightLines(doc.snapshot(), 0, 0).first().isEmpty());
    QVERIFY(!h.parsing());
  }

  // SYNTAX-09: spans keep their place through an edit while the new parse is still on its way.
  void spansShiftBeforeReparseLands() {
    TextDocument doc;
    doc.setText(u"int a;\nint b;\n"_s);
    TreeSitterHighlighter h;
    h.setFileName(u"a.cpp"_s);
    h.attach(&doc);
    settle(h);
    QCOMPARE(styleAt(h, doc, 1, 0), TokenStyle::Type);
    doc.insert(0, u"\n\n"_s); // two new lines on top: "int b;" is now line 3
    QCOMPARE(styleAt(h, doc, 3, 0), TokenStyle::Type);
    QCOMPARE(styleAt(h, doc, 3, 2), TokenStyle::Type);
    QCOMPARE(styleAt(h, doc, 3, 3), TokenStyle::Default);
  }

  // SYNTAX-07: opening a comment restyles the lines below once the parse lands, and says so.
  void openingACommentInvalidatesLaterLines() {
    TextDocument doc;
    doc.setText(u"int a;\nint b;\nint c;\nint d; /* z */\n"_s);
    TreeSitterHighlighter h;
    h.setFileName(u"a.cpp"_s);
    h.attach(&doc);
    settle(h);
    QCOMPARE(styleAt(h, doc, 2, 0), TokenStyle::Type);
    QList<std::pair<qsizetype, qsizetype>> invalidations;
    connect(&h, &Highlighter::invalidated, this, [&](qsizetype a, qsizetype b) { invalidations.append({a, b}); });
    const quint64 landed = h.stats().landed;
    doc.insert(0, u"/*"_s);
    QTRY_VERIFY(h.stats().landed > landed && !h.parsing());
    QCOMPARE(styleAt(h, doc, 2, 0), TokenStyle::Comment);
    QCOMPARE(styleAt(h, doc, 0, 0), TokenStyle::Comment);
    bool coversLine2 = false;
    for (const auto &[a, b] : std::as_const(invalidations))
      coversLine2 |= (a == Highlighter::AllLines) || (a <= 2 && b >= 2);
    QVERIFY(coversLine2);
  }

  // Edits made while the worker is busy are replayed on its tree, which ends up equal to a fresh parse.
  void editsDuringAParseAreReplayed() {
    QString text;
    for (int i = 0; i < 3000; ++i)
      text += u"int value%1 = %1; // comment\n"_s.arg(i);
    TextDocument doc;
    doc.setText(text);
    TreeSitterHighlighter h;
    h.setFileName(u"a.cpp"_s);
    h.attach(&doc);
    QVERIFY(h.parsing());
    doc.insert(4, u"x"_s);
    doc.insert(0, u"// top\n"_s);
    doc.remove(30, 36);
    doc.insert(doc.length(), u"int tail;\n"_s);
    QTRY_VERIFY_WITH_TIMEOUT(!h.parsing() && h.stats().landed >= 2, 20000);

    TextDocument fresh;
    fresh.setText(doc.snapshot().toString());
    TreeSitterHighlighter reference;
    reference.setFileName(u"a.cpp"_s);
    reference.attach(&fresh);
    settle(reference);
    QCOMPARE(h.debugTree(), reference.debugTree());
    QCOMPARE(styleAt(h, doc, 1, 0), styleAt(reference, fresh, 1, 0));
  }

  void resetDiscardsStaleResults() {
    QString text;
    for (int i = 0; i < 3000; ++i)
      text += u"int value%1 = %1;\n"_s.arg(i);
    TextDocument doc;
    doc.setText(text);
    TreeSitterHighlighter h;
    h.setFileName(u"a.cpp"_s);
    h.attach(&doc);
    QVERIFY(h.parsing());
    doc.setText(u"def f():\n    pass\n"_s);
    h.setFileName(u"a.py"_s);
    settle(h);
    QTRY_VERIFY(!h.parsing());
    QCOMPARE(h.detectedLanguage(), u"python"_s);
    expect(h, doc, u"def"_s, TokenStyle::Keyword);
    QVERIFY(h.stats().discarded >= 1);
  }

  // SYNTAX-11: above the full-parse limit only a window around the viewport is parsed, and
  // asking for lines outside it parses another.
  void windowFollowsTheViewport() {
    QString text;
    for (int i = 0; i < 3000; ++i)
      text += u"int a;\n"_s;
    TextDocument doc;
    doc.setText(text);
    TreeSitterHighlighter h;
    h.setWindowSize(2000);
    h.setFullParseLimit(10000);
    h.setFileName(u"a.cpp"_s);
    h.attach(&doc);
    settle(h);
    QVERIFY(!h.hasFullTree());
    const auto [from, to] = h.parsedRange();
    QCOMPARE(from, 0);
    QVERIFY(to < doc.length());
    QCOMPARE(styleAt(h, doc, 0, 0), TokenStyle::Type);
    QCOMPARE(styleAt(h, doc, 2900, 0), TokenStyle::Default); // not parsed yet
    QTRY_COMPARE_WITH_TIMEOUT(styleAt(h, doc, 2900, 0), TokenStyle::Type, 20000);
    QVERIFY(!h.hasFullTree());
    QVERIFY(h.parsedRange().first > 0);
  }

  // Below the limit the window is only the first step.
  void fullParseFollowsTheWindow() {
    QString text;
    for (int i = 0; i < 3000; ++i)
      text += u"int a;\n"_s;
    TextDocument doc;
    doc.setText(text);
    TreeSitterHighlighter h;
    h.setWindowSize(2000);
    h.setFullParseLimit(1'000'000);
    h.setFileName(u"a.cpp"_s);
    h.attach(&doc);
    QTRY_VERIFY_WITH_TIMEOUT(h.hasFullTree() && !h.parsing(), 20000);
    QCOMPARE(styleAt(h, doc, 2999, 0), TokenStyle::Type);
    QVERIFY(h.stats().windowParses >= 1);
    QVERIFY(h.stats().fullParses >= 1);
  }

  void destroyingWhileParsingIsSafe() {
    QString text;
    for (int i = 0; i < 20000; ++i)
      text += u"int value%1 = %1; // c\n"_s.arg(i);
    TextDocument doc;
    doc.setText(text);
    {
      TreeSitterHighlighter h;
      h.setFileName(u"a.cpp"_s);
      h.attach(&doc);
      QVERIFY(h.parsing());
    }
    QTest::qWait(300); // the worker finishes and finds nobody to tell
  }

  void blocksAreCached() {
    QString text;
    for (int i = 0; i < 200; ++i)
      text += u"int a;\n"_s;
    TextDocument doc;
    doc.setText(text);
    TreeSitterHighlighter h;
    h.setFileName(u"a.cpp"_s);
    h.attach(&doc);
    settle(h);
    for (int line = 0; line < 64; ++line)
      h.highlightLines(doc.snapshot(), line, line);
    QCOMPARE(h.stats().blocksComputed, 1u);
    h.highlightLines(doc.snapshot(), 64, 64);
    QCOMPARE(h.stats().blocksComputed, 2u);
  }
};

QTEST_GUILESS_MAIN(TstTreeSitterHighlighter)
#include "tst_treesitterhighlighter.moc"
