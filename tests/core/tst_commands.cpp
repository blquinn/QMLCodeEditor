#include "core/commands.h"
#include "testutil.h"

#include <QtTest>

using namespace qce;
using namespace Qt::StringLiterals;

namespace {

struct Fixture {
  TextDocument doc;
  SelectionSet sel{&doc};
  EditContext ctx{doc, sel, {}};
  explicit Fixture(const QString &text = {}) { doc.setText(text); }
  QString text() const { return doc.rope().toString(); }
};

} // namespace

class TstCommands : public QObject {
  Q_OBJECT
private slots:
  void typingInsertsAndAdvances() {
    Fixture f(u"ac"_s);
    f.sel.setSingle(1);
    QVERIFY(commands::insertText(f.ctx, u"b"));
    QCOMPARE(f.text(), u"abc"_s);
    QCOMPARE(f.sel.primary(), (Selection{2, 2}));
  }

  void typingReplacesSelection() {
    Fixture f(u"hello world"_s);
    f.sel.setSingle(6, 11);
    commands::insertText(f.ctx, u"there");
    QCOMPARE(f.text(), u"hello there"_s);
    QCOMPARE(f.sel.primary(), (Selection{11, 11}));
  }

  void backspaceAndDeleteAreGraphemeAware() {
    Fixture f(u"a\U0001F600éb"_s); // a, emoji (2 units), e + combining acute, b
    f.sel.setSingle(3);
    QVERIFY(commands::deleteBackward(f.ctx));
    QCOMPARE(f.text(), u"aéb"_s);
    f.sel.setSingle(1);
    QVERIFY(commands::deleteForward(f.ctx));
    QCOMPARE(f.text(), u"ab"_s);
    f.sel.setSingle(0);
    QVERIFY(!commands::deleteBackward(f.ctx)); // nothing before the start
    f.sel.setSingle(2);
    QVERIFY(!commands::deleteForward(f.ctx));
  }

  void typeText_data() {
    QTest::addColumn<QString>("before"); // '|' cursor, '[' ']' selection (head at ']')
    QTest::addColumn<QString>("typed");
    QTest::addColumn<QString>("after");
    QTest::newRow("open") << u"a |"_s << u"("_s << u"a (|)"_s;
    QTest::newRow("brace before text") << u"|x"_s << u"{"_s << u"{|x"_s;
    QTest::newRow("before closer") << u"(|)"_s << u"["_s << u"([|])"_s;
    QTest::newRow("type over") << u"(a|)"_s << u")"_s << u"(a)|"_s;
    QTest::newRow("closer without match") << u"a|"_s << u")"_s << u"a)|"_s;
    QTest::newRow("quote") << u"x = |"_s << u"\""_s << u"x = \"|\""_s;
    QTest::newRow("quote after word") << u"don|"_s << u"'"_s << u"don'|"_s;
    QTest::newRow("quote type over") << u"\"a|\""_s << u"\""_s << u"\"a\"|"_s;
    QTest::newRow("double quote") << u"'|'"_s << u"'"_s << u"''|"_s;
    QTest::newRow("wrap") << u"a [b]c"_s << u"("_s << u"a ([b])c"_s;
    QTest::newRow("selection replaced by closer") << u"a [b]c"_s << u")"_s << u"a )|c"_s;
    QTest::newRow("plain letter") << u"|"_s << u"x"_s << u"x|"_s;
  }
  void typeText() {
    QFETCH(QString, before);
    QFETCH(QString, typed);
    QFETCH(QString, after);
    auto parse = [](QString s, qsizetype &a, qsizetype &h) {
      a = h = -1;
      if (const qsizetype c = s.indexOf(u'|'); c >= 0) {
        a = h = c;
        s.remove(c, 1);
      } else if (const qsizetype o = s.indexOf(u'['); o >= 0) {
        s.remove(o, 1);
        a = o;
        h = s.indexOf(u']');
        s.remove(h, 1);
      }
      return s;
    };
    qsizetype a, h, ea, eh;
    Fixture f(parse(before, a, h));
    f.sel.setSingle(a, h);
    QVERIFY(commands::typeText(f.ctx, typed));
    QCOMPARE(f.text(), parse(after, ea, eh));
    if (ea >= 0)
      QCOMPARE(f.sel.primary(), (Selection{ea, eh}));
  }

  void typeTextOffLeavesTypingAlone() {
    Fixture f(u"x"_s);
    f.ctx.settings.autoClose = false;
    f.sel.setSingle(1);
    commands::typeText(f.ctx, u"(");
    QCOMPARE(f.text(), u"x("_s);
  }

  void typeTextCustomPairs() {
    Fixture f;
    f.ctx.settings.autoClosePairs = {{u'<', u'>'}};
    commands::typeText(f.ctx, u"<");
    commands::typeText(f.ctx, u"(");
    QCOMPARE(f.text(), u"<(>"_s);
  }

  void typeTextMultiCursor() {
    Fixture f(u"a\nb(\n)"_s);
    f.sel.set({{1, 1}, {4, 4}, {6, 6}});
    // Pairs before a line break and at the end of the text.
    commands::typeText(f.ctx, u"(");
    QCOMPARE(f.text(), u"a()\nb(()\n)()"_s);
    QCOMPARE(f.sel.selections(), (SelectionList{{2, 2}, {7, 7}, {11, 11}}));
    commands::undo(f.ctx);
    QCOMPARE(f.text(), u"a\nb(\n)"_s);
  }

  void typeTextMixedOverAndPair() {
    Fixture f(u"(a)\n"_s);
    f.sel.set({{2, 2}, {4, 4}});
    commands::typeText(f.ctx, u")");
    QCOMPARE(f.text(), u"(a)\n)"_s); // the second cursor has no ')' ahead: plain insert
    Fixture g(u"()\n()"_s);
    g.sel.set({{1, 1}, {4, 4}});
    commands::typeText(g.ctx, u")");
    QCOMPARE(g.text(), u"()\n()"_s);
    QCOMPARE(g.sel.selections(), (SelectionList{{2, 2}, {5, 5}}));
  }

  void typeTextUndoRemovesWholePair() {
    Fixture f;
    commands::typeText(f.ctx, u"(");
    commands::undo(f.ctx);
    QCOMPARE(f.text(), QString());
  }

  void backspaceDeletesEmptyPair() {
    Fixture f(u"f(\"\")"_s);
    f.sel.setSingle(3);
    commands::deleteBackward(f.ctx);
    QCOMPARE(f.text(), u"f()"_s);
    commands::deleteBackward(f.ctx);
    QCOMPARE(f.text(), u"f"_s);
    Fixture g(u"(a)"_s);
    g.sel.setSingle(2);
    commands::deleteBackward(g.ctx);
    QCOMPARE(g.text(), u"()"_s);
    g.ctx.settings.autoClose = false;
    g.sel.setSingle(1);
    commands::deleteBackward(g.ctx);
    QCOMPARE(g.text(), u")"_s);
  }

  void newlineBetweenBracketsExpands() {
    Fixture f(u"  if (x) {}"_s);
    f.sel.setSingle(10);
    QVERIFY(commands::newline(f.ctx));
    QCOMPARE(f.text(), u"  if (x) {\n      \n  }"_s);
    QCOMPARE(f.sel.primary(), (Selection{17, 17}));
    commands::undo(f.ctx);
    QCOMPARE(f.text(), u"  if (x) {}"_s);
  }

  void newlineExpansionFollowsIndentSettings() {
    Fixture f(u"\t{}"_s);
    f.ctx.settings.insertSpaces = false;
    f.sel.setSingle(2);
    commands::newline(f.ctx);
    QCOMPARE(f.text(), u"\t{\n\t\t\n\t}"_s);
    Fixture g(u"[]"_s);
    g.ctx.settings.indentWidth = 2;
    g.sel.setSingle(1);
    commands::newline(g.ctx);
    QCOMPARE(g.text(), u"[\n  \n]"_s);
  }

  void newlineExpansionOnlyForBracketsAndWhenOn() {
    Fixture f(u"\"\""_s);
    f.sel.setSingle(1);
    commands::newline(f.ctx);
    QCOMPARE(f.text(), u"\"\n\""_s);
    Fixture g(u"{}"_s);
    g.ctx.settings.autoClose = false;
    g.sel.setSingle(1);
    commands::newline(g.ctx);
    QCOMPARE(g.text(), u"{\n}"_s);
  }

  void newlineExpansionKeepsCrlf() {
    Fixture f(u"a\r\n{}"_s);
    FileFormat format;
    format.dominantLineEnding = LineEnding::Crlf;
    f.doc.setFormat(format);
    f.sel.setSingle(4);
    commands::newline(f.ctx);
    QCOMPARE(f.text(), u"a\r\n{\r\n    \r\n}"_s);
  }

  void cursorsThatCannotDeleteStayInTheSet() {
    Fixture f(u"abc"_s);
    f.sel.set({{0, 0}, {2, 2}});
    QVERIFY(commands::deleteBackward(f.ctx)); // the cursor at 0 has nothing to delete
    QCOMPARE(f.text(), u"ac"_s);
    QCOMPARE(f.sel.selections(), (SelectionList{{0, 0}, {1, 1}}));
  }

  void backspaceOverCrlfRemovesBoth() {
    Fixture f(u"a\r\nb"_s);
    f.sel.setSingle(3);
    commands::deleteBackward(f.ctx);
    QCOMPARE(f.text(), u"ab"_s);
  }

  void backspaceWithSelectionDeletesIt() {
    Fixture f(u"abcdef"_s);
    f.sel.setSingle(4, 1);
    commands::deleteBackward(f.ctx);
    QCOMPARE(f.text(), u"aef"_s);
    QCOMPARE(f.sel.primary(), (Selection{1, 1}));
  }

  void multiCursorEditIsOneUndoStep() {
    Fixture f(u"aaa\nbbb\nccc"_s);
    f.sel.set({{1, 1}, {5, 5}, {9, 9}});
    commands::insertText(f.ctx, u"X");
    QCOMPARE(f.text(), u"aXaa\nbXbb\ncXcc"_s);
    QCOMPARE(f.sel.selections(), (SelectionList{{2, 2}, {7, 7}, {12, 12}}));
    QVERIFY(commands::undo(f.ctx));
    QCOMPARE(f.text(), u"aaa\nbbb\nccc"_s);
    QCOMPARE(f.sel.selections(), (SelectionList{{1, 1}, {5, 5}, {9, 9}}));
    QVERIFY(!commands::undo(f.ctx));
    QVERIFY(commands::redo(f.ctx));
    QCOMPARE(f.text(), u"aXaa\nbXbb\ncXcc"_s);
    QCOMPARE(f.sel.selections(), (SelectionList{{2, 2}, {7, 7}, {12, 12}}));
  }

  void typingRunsMergeIntoOneUndoStep() {
    Fixture f;
    for (const char16_t c : {u'a', u'b', u'c'})
      commands::insertText(f.ctx, QStringView(&c, 1));
    QCOMPARE(f.text(), u"abc"_s);
    QCOMPARE(f.doc.undoStack().undoSteps(), 1);
    commands::undo(f.ctx);
    QCOMPARE(f.text(), QString());
    QCOMPARE(f.sel.primary(), (Selection{0, 0}));
  }

  void multiCursorTypingRunsMergeIntoOneUndoStep() {
    Fixture f(u"aaa\nbbb\nccc"_s);
    f.sel.set({{0, 0}, {4, 4}, {8, 8}});
    for (const char16_t c : {u'x', u'y', u'z'})
      commands::insertText(f.ctx, QStringView(&c, 1));
    QCOMPARE(f.text(), u"xyzaaa\nxyzbbb\nxyzccc"_s);
    QCOMPARE(f.doc.undoStack().undoSteps(), 1);
    // moving the cursors ends the run
    commands::move(f.ctx, Movement::CharRight);
    commands::insertText(f.ctx, u"!");
    QCOMPARE(f.doc.undoStack().undoSteps(), 2);
    commands::undo(f.ctx);
    QCOMPARE(f.text(), u"xyzaaa\nxyzbbb\nxyzccc"_s);
    commands::undo(f.ctx);
    QCOMPARE(f.text(), u"aaa\nbbb\nccc"_s);
    QCOMPARE(f.sel.selections(), (SelectionList{{0, 0}, {4, 4}, {8, 8}}));
  }

  void multiCursorBackspaceRunsMerge() {
    Fixture f(u"abc\nabc\nabc"_s);
    f.sel.set({{3, 3}, {7, 7}, {11, 11}});
    commands::deleteBackward(f.ctx);
    commands::deleteBackward(f.ctx);
    QCOMPARE(f.text(), u"a\na\na"_s);
    QCOMPARE(f.doc.undoStack().undoSteps(), 1);
    commands::undo(f.ctx);
    QCOMPARE(f.text(), u"abc\nabc\nabc"_s);
    QCOMPARE(f.sel.selections(), (SelectionList{{3, 3}, {7, 7}, {11, 11}}));
  }

  void multiCursorLineEditsAreSingleSteps() {
    Fixture f(u"  a\n  b\n  c"_s);
    f.sel.set({{3, 3}, {7, 7}, {11, 11}});
    commands::newline(f.ctx);
    QCOMPARE(f.text(), u"  a\n  \n  b\n  \n  c\n  "_s);
    QCOMPARE(f.doc.undoStack().undoSteps(), 1);
    commands::undo(f.ctx);
    QCOMPARE(f.text(), u"  a\n  b\n  c"_s);
    commands::indent(f.ctx);
    commands::undo(f.ctx);
    QCOMPARE(f.text(), u"  a\n  b\n  c"_s);
    QCOMPARE(f.sel.selections(), (SelectionList{{3, 3}, {7, 7}, {11, 11}}));
    commands::deleteWordBackward(f.ctx);
    QCOMPARE(f.doc.undoStack().undoSteps(), 1);
    commands::undo(f.ctx);
    QCOMPARE(f.text(), u"  a\n  b\n  c"_s);
  }

  void backspaceInIndentationMovesByStops() {
    Fixture f(u"        x"_s);
    f.sel.setSingle(8);
    commands::deleteBackward(f.ctx);
    QCOMPARE(f.text(), u"    x"_s);
    QCOMPARE(f.sel.primary(), (Selection{4, 4}));
    commands::deleteBackward(f.ctx);
    QCOMPARE(f.text(), u"x"_s);
  }

  void backspaceBetweenStopsGoesToPreviousStop() {
    Fixture f(u"      x"_s);
    f.sel.setSingle(6);
    commands::deleteBackward(f.ctx);
    QCOMPARE(f.text(), u"    x"_s);
  }

  void backspaceAfterTextDeletesOneSpace() {
    Fixture f(u"a    b"_s);
    f.sel.setSingle(5);
    commands::deleteBackward(f.ctx);
    QCOMPARE(f.text(), u"a   b"_s);
  }

  void backspaceInTabIndentationDeletesOneTab() {
    Fixture f(u"\t\tx"_s);
    f.sel.setSingle(2);
    commands::deleteBackward(f.ctx);
    QCOMPARE(f.text(), u"\tx"_s);
  }

  void backspaceInIndentationWithSeveralCursors() {
    Fixture f(u"      a\n        b"_s);
    f.sel.set({{6, 6}, {16, 16}});
    commands::deleteBackward(f.ctx);
    QCOMPARE(f.text(), u"    a\n    b"_s);
    QCOMPARE(f.doc.undoStack().undoSteps(), 1);
  }

  void outdentGoesToPreviousStop() {
    Fixture f(u"      x"_s);
    f.sel.setSingle(7);
    commands::outdent(f.ctx);
    QCOMPARE(f.text(), u"    x"_s);
    commands::outdent(f.ctx);
    QCOMPARE(f.text(), u"x"_s);
  }

  void pasteDistributesLinesAcrossCursors() {
    Fixture f(u"1:\n2:\n3:"_s);
    f.sel.set({{2, 2}, {5, 5}, {8, 8}});
    QVERIFY(commands::paste(f.ctx, u"a\nb\nc\n"_s)); // a trailing line break is ignored
    QCOMPARE(f.text(), u"1:a\n2:b\n3:c"_s);
    QCOMPARE(f.doc.undoStack().undoSteps(), 1);
    commands::undo(f.ctx);
    // a different count pastes everything everywhere
    QVERIFY(commands::paste(f.ctx, u"x\ny"_s));
    QCOMPARE(f.text(), u"1:x\ny\n2:x\ny\n3:x\ny"_s);
  }

  void pasteUsesClipboardPiecesWhenGiven() {
    Fixture f(u"[]\n[]"_s);
    f.sel.set({{1, 1}, {4, 4}});
    QVERIFY(commands::paste(f.ctx, u"p\nq\nr"_s, {u"p\nq"_s, u"r"_s}));
    QCOMPARE(f.text(), u"[p\nq]\n[r]"_s);
    // a lone cursor takes the whole text
    f.sel.setSingle(0);
    QVERIFY(commands::paste(f.ctx, u"a\nb"_s));
    QCOMPARE(f.text().left(3), u"a\nb"_s);
  }

  void undoRestoresSelection() {
    Fixture f(u"hello world"_s);
    f.sel.setSingle(0, 5);
    commands::insertText(f.ctx, u"bye", EditKind::Other);
    QCOMPARE(f.text(), u"bye world"_s);
    commands::undo(f.ctx);
    QCOMPARE(f.sel.primary(), (Selection{0, 5}));
  }

  void selectAllSelectsEverything() {
    Fixture f(u"abc\ndef"_s);
    commands::selectAll(f.ctx);
    QCOMPARE(f.sel.primary(), (Selection{0, 7}));
  }

  void refusedWhileLoading() {
    Fixture f(u"abc"_s);
    f.doc.load(QStringLiteral("/nonexistent/file")); // starts a load; edits are refused until it ends
    if (f.doc.isLoading())
      QVERIFY(!commands::insertText(f.ctx, u"x"));
    f.doc.cancelLoad();
  }

  // Random selections and edits against a plain QString model.
  void differentialAgainstStringModel() {
    test::Random rng(test::testSeed());
    for (int round = 0; round < test::testIterations(200); ++round) {
      QString model = rng.text(rng.range(0, 60));
      Fixture f(model);
      // Random, non-touching selections on code point boundaries.
      QList<Selection> sels;
      qsizetype pos = 0;
      while (pos < model.size() && sels.size() < 5) {
        const qsizetype a = f.doc.rope().snapToCodePoint(qMin<qsizetype>(model.size(), pos + rng.below(8)));
        const qsizetype b = f.doc.rope().snapToCodePoint(qMin<qsizetype>(model.size(), a + (rng.chance(50) ? 0 : rng.below(5))));
        if (a < pos)
          break;
        sels.append({a, b});
        pos = b + 1;
      }
      if (sels.isEmpty())
        sels.append({0, 0});
      f.sel.set(sels);
      const SelectionList before = f.sel.selections();
      const QString text = rng.chance(30) ? QString() : rng.text(rng.range(1, 4));
      if (text.isEmpty() && std::all_of(before.begin(), before.end(), [](Selection s) { return s.isEmpty(); }))
        continue;

      // The model: replace back to front.
      QString expected = model;
      for (qsizetype i = before.size() - 1; i >= 0; --i)
        expected.replace(before[i].start(), before[i].end() - before[i].start(), text);
      // Skip texts whose joins form a CRLF with neighbours: the document widens those edits.
      if (!commands::insertText(f.ctx, text, EditKind::Other))
        continue;
      const bool joinsCrlf = f.text() != expected;
      if (!joinsCrlf) {
        QCOMPARE(f.text(), expected);
        QCOMPARE(f.sel.count(), int(before.size()));
      }
      // Undo always round-trips exactly.
      QVERIFY(commands::undo(f.ctx));
      QCOMPARE(f.text(), model);
      QCOMPARE(f.sel.selections(), before);
    }
  }
};

QTEST_APPLESS_MAIN(TstCommands)
#include "tst_commands.moc"
