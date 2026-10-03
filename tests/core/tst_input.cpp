#include "core/commands.h"
#include "core/cursorlayout.h"
#include "core/inputhandler.h"

#include <QtTest>

using namespace qce;
using namespace Qt::StringLiterals;

namespace {

struct Host : InputHost {
  int copies = 0, cuts = 0, pastes = 0;
  qsizetype scrolled = 0;
  void copy() override { ++copies; }
  void cut() override { ++cuts; }
  void paste() override { ++pastes; }
  void scrollRows(qsizetype rows) override { scrolled += rows; }
  void foldCommand(FoldCommand command) override { folds.append(command); }
  QList<FoldCommand> folds;
};

struct Fixture {
  TextDocument doc;
  SelectionSet sel{&doc};
  DisplayMap map{&doc};
  GridCursorLayout layout{&doc, 4, 3, &map};
  EditContext ctx{doc, sel, {}, &map, &layout};
  DefaultInputHandler handler;
  Host host;

  explicit Fixture(const QString &text = {}) { doc.setText(text); }
  QString text() const { return doc.rope().toString(); }
  bool key(Qt::Key key, Qt::KeyboardModifiers mods = {}, const QString &text = {}) {
    QKeyEvent event(QEvent::KeyPress, key, mods, text);
    return handler.keyPress(&event, ctx, host);
  }
  bool type(const QString &text) { return key(Qt::Key_unknown, {}, text); }
  Selection cursor() const { return sel.primary(); }
};

} // namespace

class TstInput : public QObject {
  Q_OBJECT
private slots:
  void typingAndBackspace() {
    Fixture f;
    f.type(u"h"_s);
    f.type(u"i"_s);
    QCOMPARE(f.text(), u"hi"_s);
    f.key(Qt::Key_Backspace);
    QCOMPARE(f.text(), u"h"_s);
    f.key(Qt::Key_Home);
    f.key(Qt::Key_Delete);
    QCOMPARE(f.text(), QString());
  }

  void controlKeysDoNotInsertText() {
    Fixture f;
    QVERIFY(!f.key(Qt::Key_F, Qt::ControlModifier, u"\x06"_s));
    QVERIFY(!f.key(Qt::Key_Escape, {}, u"\x1b"_s));
    QCOMPARE(f.text(), QString());
    QVERIFY(f.key(Qt::Key_At, Qt::ControlModifier | Qt::AltModifier, u"@"_s)); // AltGr
    QCOMPARE(f.text(), u"@"_s);
  }

  void enterInsertsLineBreakInDocumentsStyle() {
    Fixture f(u"ab"_s);
    f.sel.setSingle(1);
    f.key(Qt::Key_Return);
    QCOMPARE(f.text(), u"a\nb"_s);
    FileFormat format;
    format.dominantLineEnding = LineEnding::Crlf;
    f.doc.setFormat(format);
    f.key(Qt::Key_Return);
    QCOMPARE(f.text(), u"a\n\r\nb"_s);
  }

  void clipboardKeysGoToTheHost() {
    Fixture f(u"abc"_s);
    f.key(Qt::Key_C, Qt::ControlModifier);
    f.key(Qt::Key_X, Qt::ControlModifier);
    f.key(Qt::Key_V, Qt::ControlModifier);
    QCOMPARE(f.host.copies, 1);
    QCOMPARE(f.host.cuts, 1);
    QCOMPARE(f.host.pastes, 1);
  }

  void undoRedoKeys() {
    Fixture f;
    f.type(u"a"_s);
    f.key(Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(f.text(), QString());
    f.key(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(f.text(), u"a"_s);
  }

  void arrowsMoveByGraphemeAndCollapseSelections() {
    Fixture f(u"a\U0001F600éb"_s);
    f.sel.setSingle(0);
    f.key(Qt::Key_Right);
    f.key(Qt::Key_Right);
    QCOMPARE(f.cursor(), (Selection{3, 3}));
    f.key(Qt::Key_Right);
    QCOMPARE(f.cursor(), (Selection{5, 5}));
    f.key(Qt::Key_Left);
    QCOMPARE(f.cursor(), (Selection{3, 3}));
    f.key(Qt::Key_Left, Qt::ShiftModifier);
    QCOMPARE(f.cursor(), (Selection{3, 1}));
    f.key(Qt::Key_Right); // collapses to the right edge
    QCOMPARE(f.cursor(), (Selection{3, 3}));
    f.sel.setSingle(1, 5);
    f.key(Qt::Key_Left); // collapses to the left edge
    QCOMPARE(f.cursor(), (Selection{1, 1}));
  }

  void wordMovementAndSelection() {
    Fixture f(u"foo bar  baz"_s);
    f.sel.setSingle(0);
    f.key(Qt::Key_Right, Qt::ControlModifier);
    QCOMPARE(f.cursor().head, 4);
    f.key(Qt::Key_Right, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(f.cursor(), (Selection{4, 9}));
    f.key(Qt::Key_Left, Qt::ControlModifier);
    QCOMPARE(f.cursor().head, 4);
    f.key(Qt::Key_Backspace, Qt::ControlModifier);
    QCOMPARE(f.text(), u"bar  baz"_s);
    f.key(Qt::Key_Delete, Qt::ControlModifier);
    QCOMPARE(f.text(), u"baz"_s);
  }

  void homeTogglesBetweenIndentAndColumnZero() {
    Fixture f(u"    code"_s);
    f.sel.setSingle(7);
    f.key(Qt::Key_Home);
    QCOMPARE(f.cursor().head, 4);
    f.key(Qt::Key_Home);
    QCOMPARE(f.cursor().head, 0);
    f.key(Qt::Key_Home);
    QCOMPARE(f.cursor().head, 4);
    f.key(Qt::Key_End);
    QCOMPARE(f.cursor().head, 8);
    f.key(Qt::Key_Home, Qt::ShiftModifier);
    QCOMPARE(f.cursor(), (Selection{8, 4}));
  }

  void endStopsBeforeCrlf() {
    Fixture f(u"ab\r\ncd"_s);
    f.sel.setSingle(0);
    f.key(Qt::Key_End);
    QCOMPARE(f.cursor().head, 2);
  }

  void documentStartAndEnd() {
    Fixture f(u"a\nb\nc"_s);
    f.sel.setSingle(2);
    f.key(Qt::Key_End, Qt::ControlModifier);
    QCOMPARE(f.cursor().head, 5);
    f.key(Qt::Key_Home, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(f.cursor(), (Selection{5, 0}));
  }

  void verticalMovementKeepsGoalColumn() {
    Fixture f(u"abcdef\nab\nabcdef"_s);
    f.sel.setSingle(5);
    f.key(Qt::Key_Down);
    QCOMPARE(f.cursor().head, 9); // clamped to the end of the short line
    f.key(Qt::Key_Down);
    QCOMPARE(f.cursor().head, 10 + 5); // back to column 5 of the last line
    f.key(Qt::Key_Left);
    f.key(Qt::Key_Up); // a horizontal move forgets the goal
    QCOMPARE(f.cursor().head, 9);
  }

  void verticalMovementTabsUseCells() {
    Fixture f(u"\tx\nabcdefgh"_s);
    f.sel.setSingle(1); // after the tab: cell 4
    f.key(Qt::Key_Down);
    QCOMPARE(f.cursor().head, 3 + 4); // column 4 of the second line
  }

  void upFromFirstRowGoesToStartAndDownFromLastToEnd() {
    Fixture f(u"abc\ndef"_s);
    f.sel.setSingle(2);
    f.key(Qt::Key_Up);
    QCOMPARE(f.cursor().head, 0);
    f.key(Qt::Key_Down);
    f.key(Qt::Key_Down);
    QCOMPARE(f.cursor().head, 7);
  }

  void extendingVerticallyKeepsAnchor() {
    Fixture f(u"abc\ndef\nghi"_s);
    f.sel.setSingle(1);
    f.key(Qt::Key_Down, Qt::ShiftModifier);
    f.key(Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(f.cursor(), (Selection{1, 9}));
    f.key(Qt::Key_Up, Qt::ShiftModifier);
    QCOMPARE(f.cursor(), (Selection{1, 5}));
  }

  void pageMovesAndAsksHostToScroll() {
    QString text;
    for (int i = 0; i < 20; ++i)
      text += u"line\n"_s;
    Fixture f(text);
    f.sel.setSingle(0);
    f.key(Qt::Key_PageDown);
    QCOMPARE(f.doc.rope().positionAt(f.cursor().head).line, 3); // page = 3 rows
    QCOMPARE(f.host.scrolled, 3);
    f.key(Qt::Key_PageUp);
    QCOMPARE(f.cursor().head, 0);
    QCOMPARE(f.host.scrolled, 0);
  }

  // ---- Wrapped rows (WRAP-07) ----

  static WrapConfig wrapAt(int column, bool word = false) {
    WrapConfig c;
    c.mode = WrapMode::Column;
    c.column = column;
    c.wordBreak = word;
    c.measure = std::make_shared<GridWrapMeasure>(4);
    return c;
  }

  void upAndDownMoveByWrappedRowsAndKeepTheGoal() {
    Fixture f(u"abcdefghijklmnopqrstuvwxyz\nshort\nabcdefghijklmnopqrstuvwxyz"_s);
    f.map.setWrapConfig(wrapAt(10)); // line 0 rows: [0,10) [10,20) [20,26)
    f.sel.setSingle(7);
    f.key(Qt::Key_Down);
    QCOMPARE(f.cursor().head, 17);
    f.key(Qt::Key_Down);
    QCOMPARE(f.cursor().head, 26); // the last row is shorter than the goal column
    f.key(Qt::Key_Down);
    QCOMPARE(f.cursor().head, 27 + 5); // "short"
    f.key(Qt::Key_Down);
    QCOMPARE(f.cursor().head, 33 + 7); // the goal column survived two short rows
    f.key(Qt::Key_Up);
    f.key(Qt::Key_Up);
    QCOMPARE(f.cursor().head, 26);
    f.key(Qt::Key_Up);
    QCOMPARE(f.cursor().head, 17);
    f.key(Qt::Key_Up);
    QCOMPARE(f.cursor().head, 7);
    f.key(Qt::Key_Up);
    QCOMPARE(f.cursor().head, 0); // above the first row: the start
  }

  void goalColumnCountsTheHangingIndent() {
    Fixture f(u"    abcdefghijklmnopqrstuvwxyz\nabcdefghijklmnopqrstuvwxyz"_s);
    WrapConfig config = wrapAt(10);
    config.hangingIndent = true; // continuation rows start 4 cells in and hold 6 characters
    f.map.setWrapConfig(config);
    f.sel.setSingle(6); // row 0, cell 6
    f.key(Qt::Key_Down);
    QCOMPARE(f.cursor().head, 10 + 2); // row 1 starts at column 10 at cell 4, so cell 6 is two in
    f.key(Qt::Key_Down);
    QCOMPARE(f.cursor().head, 16 + 2);
  }

  void homeAndEndGoToRowEdgesFirst() {
    Fixture f(u"abcdefghijklmnopqrstuvwxyz"_s);
    f.map.setWrapConfig(wrapAt(10)); // rows [0,10) [10,20) [20,26)
    f.sel.setSingle(15);
    f.key(Qt::Key_Home);
    QCOMPARE(f.cursor().head, 10);
    f.key(Qt::Key_Home);
    QCOMPARE(f.cursor().head, 0); // second press: the line
    f.sel.setSingle(12);
    f.key(Qt::Key_End);
    QCOMPARE(f.cursor().head, 19); // the last place on the row; the break itself is on the next one
    f.key(Qt::Key_End);
    QCOMPARE(f.cursor().head, 26);
    f.key(Qt::Key_End);
    QCOMPARE(f.cursor().head, 26);
    f.key(Qt::Key_Home);
    QCOMPARE(f.cursor().head, 20);
    // Selecting with the same keys.
    f.sel.setSingle(12);
    f.key(Qt::Key_End, Qt::ShiftModifier);
    QCOMPARE(f.cursor(), (Selection{12, 19}));
    f.key(Qt::Key_Home, Qt::ShiftModifier);
    QCOMPARE(f.cursor(), (Selection{12, 10}));
  }

  void homeOnTheFirstRowIsStillSmart() {
    Fixture f(u"    abcdefghijklmnopqrstuvwxyz"_s);
    f.map.setWrapConfig(wrapAt(10));
    f.sel.setSingle(8);
    f.key(Qt::Key_Home);
    QCOMPARE(f.cursor().head, 4);
    f.key(Qt::Key_Home);
    QCOMPARE(f.cursor().head, 0);
  }

  void wordWrappedRowsEndBeforeTheirTrailingSpace() {
    Fixture f(u"hello world foo bar"_s);
    f.map.setWrapConfig(wrapAt(10, true)); // rows "hello " "world foo " "bar"
    f.sel.setSingle(2);
    f.key(Qt::Key_End);
    QCOMPARE(f.cursor().head, 5);
  }

  void pageMovesByWrappedRows() {
    Fixture f(u"abcdefghijklmnopqrstuvwxyz\nnext line"_s);
    f.map.setWrapConfig(wrapAt(10));
    f.sel.setSingle(0);
    f.key(Qt::Key_PageDown); // 3 rows: lands on the first row of line 1
    QCOMPARE(f.cursor().head, 27);
    QCOMPARE(f.host.scrolled, 3);
  }

  void movementEndsTypingRun() {
    Fixture f;
    f.type(u"a"_s);
    f.key(Qt::Key_Left);
    f.key(Qt::Key_Right);
    f.type(u"b"_s);
    QCOMPARE(f.doc.undoStack().undoSteps(), 2);
  }

  void readOnlyBlocksEditsButNotMovement() {
    Fixture f(u"abc"_s);
    f.ctx.settings.readOnly = true;
    f.type(u"x"_s);
    f.key(Qt::Key_Delete);
    f.key(Qt::Key_Return);
    QCOMPARE(f.text(), u"abc"_s);
    f.key(Qt::Key_Right);
    QCOMPARE(f.cursor().head, 1);
  }

  void enterKeepsIndentationUpToTheCursor() {
    Fixture f(u"    foo"_s);
    f.sel.setSingle(6);
    f.key(Qt::Key_Return);
    QCOMPARE(f.text(), u"    fo\n    o"_s);
    f.sel.setSingle(2); // inside the indentation: only that much carries over
    f.key(Qt::Key_Return);
    QCOMPARE(f.text(), u"  \n    fo\n    o"_s);
    f.sel.setSingle(0);
    f.key(Qt::Key_Return);
    QCOMPARE(f.text().left(3), u"\n  "_s);
  }

  void tabInsertsToNextStop() {
    Fixture f(u"ab"_s);
    f.sel.setSingle(2);
    f.key(Qt::Key_Tab);
    QCOMPARE(f.text(), u"ab  "_s); // indent width 4: two spaces reach column 4
    f.ctx.settings.insertSpaces = false;
    f.key(Qt::Key_Tab);
    QCOMPARE(f.text(), u"ab  \t"_s);
  }

  void tabOnMultiLineSelectionIndentsLines() {
    Fixture f(u"a\n\nb\nc"_s);
    f.sel.setSingle(0, 4); // from the start of line 0 into line 2
    f.key(Qt::Key_Tab);
    QCOMPARE(f.text(), u"    a\n\n    b\nc"_s); // the blank line stays blank
    QCOMPARE(f.cursor().start(), 4); // the first line's start follows the text it was in front of
    QCOMPARE(f.doc.undoStack().undoSteps(), 1);
    f.key(Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(f.text(), u"a\n\nb\nc"_s);
    QCOMPARE(f.cursor(), (Selection{0, 4}));
  }

  void selectionEndingAtLineStartLeavesThatLineAlone() {
    Fixture f(u"a\nb\nc"_s);
    f.sel.setSingle(0, 4); // ends at the start of line 2
    f.key(Qt::Key_Tab);
    QCOMPARE(f.text(), u"    a\n    b\nc"_s);
  }

  void backtabOutdentsLines() {
    Fixture f(u"        a\n  b\n\tc\nd"_s);
    f.sel.setSingle(0, f.doc.length());
    f.key(Qt::Key_Backtab, Qt::ShiftModifier);
    QCOMPARE(f.text(), u"    a\nb\nc\nd"_s);
    f.sel.setSingle(2); // an empty cursor outdents its own line
    f.key(Qt::Key_Backtab, Qt::ShiftModifier);
    QCOMPARE(f.text(), u"a\nb\nc\nd"_s);
  }

  void movesEveryCursor() {
    Fixture f(u"ab\ncd"_s);
    f.sel.set({{0, 0}, {3, 3}});
    f.key(Qt::Key_Right);
    QCOMPARE(f.sel.selections(), (SelectionList{{1, 1}, {4, 4}}));
    f.key(Qt::Key_Right);
    f.key(Qt::Key_Right); // the first cursor crosses to the next line start; the second reaches the end
    QCOMPARE(f.sel.selections(), (SelectionList{{3, 3}, {5, 5}}));
  }

  // ---- Folds (M7) ----------------------------------------------------------------------------

  void movementStepsOverFoldedLines() {
    Fixture f(u"head {\nbody1\nbody2\n}\ntail"_s);
    f.map.fold(0, 2); // hides "body1" and "body2"
    f.sel.setSingle(f.doc.rope().lineEnd(0));
    commands::move(f.ctx, Movement::CharRight);
    QCOMPARE(f.doc.rope().lineAt(f.sel.primary().head), 3);
    QCOMPARE(f.sel.primary().head, f.doc.rope().lineStart(3));
    commands::move(f.ctx, Movement::CharLeft);
    QCOMPARE(f.sel.primary().head, f.doc.rope().lineEnd(0));
    commands::move(f.ctx, Movement::WordRight);
    QVERIFY(!f.map.folds().isHidden(f.doc.rope().lineAt(f.sel.primary().head)));
    f.sel.setSingle(f.doc.rope().lineStart(3));
    commands::move(f.ctx, Movement::WordLeft);
    QVERIFY(!f.map.folds().isHidden(f.doc.rope().lineAt(f.sel.primary().head)));
    // Shift-selection across the fold keeps the head on a visible line.
    f.sel.setSingle(f.doc.rope().lineEnd(0));
    commands::move(f.ctx, Movement::CharRight, true);
    QCOMPARE(f.sel.primary().anchor, f.doc.rope().lineEnd(0));
    QCOMPARE(f.sel.primary().head, f.doc.rope().lineStart(3));
  }

  void foldKeysAskTheHost() {
    Fixture f(u"x"_s);
    constexpr auto cs = Qt::ControlModifier | Qt::ShiftModifier;
    constexpr auto ca = Qt::ControlModifier | Qt::AltModifier;
    QVERIFY(f.key(Qt::Key_BracketLeft, cs));
    QVERIFY(f.key(Qt::Key_BraceRight, cs)); // Shift turns the bracket keys into braces on a US layout
    QVERIFY(f.key(Qt::Key_BracketLeft, ca));
    QVERIFY(f.key(Qt::Key_BracketRight, ca));
    QCOMPARE(
      f.host.folds, (QList<FoldCommand>{
                      FoldCommand::FoldAtCursor, FoldCommand::UnfoldAtCursor, FoldCommand::FoldAll,
                      FoldCommand::UnfoldAll})
    );
    QCOMPARE(f.text(), u"x"_s);
  }

  void movementEntersFoldsWhenSkippingIsOff() {
    Fixture f(u"head {\nbody1\nbody2\n}\ntail"_s);
    f.map.fold(0, 2);
    f.ctx.settings.skipFolds = false;
    f.sel.setSingle(f.doc.rope().lineEnd(0));
    commands::move(f.ctx, Movement::CharRight);
    QCOMPARE(f.doc.rope().lineAt(f.sel.primary().head), 1);
  }

  void verticalMovementSkipsFoldsAndDocEndStaysVisible() {
    Fixture f(u"head {\nbody1\nbody2\n}\ntail\nlast {\n  in\n  more"_s);
    f.map.fold(0, 2);
    f.map.fold(5, 7); // folds to the end of the text
    f.sel.setSingle(0);
    commands::move(f.ctx, Movement::RowDown);
    QCOMPARE(f.doc.rope().lineAt(f.sel.primary().head), 3);
    commands::move(f.ctx, Movement::DocEnd);
    QCOMPARE(f.doc.rope().lineAt(f.sel.primary().head), 5); // the end of the last visible line
    QCOMPARE(f.sel.primary().head, f.doc.rope().lineEnd(5));
  }

  void addCursorsAboveAndBelow() {
    Fixture f(u"abcdef\nab\nabcdef\n\nabcdef"_s);
    f.sel.setSingle(4);
    const auto mods = Qt::ControlModifier | Qt::AltModifier;
    QVERIFY(f.key(Qt::Key_Down, mods));
    QVERIFY(f.key(Qt::Key_Down, mods));
    QVERIFY(f.key(Qt::Key_Down, mods));
    QVERIFY(f.key(Qt::Key_Down, mods));
    // the goal column survives the short lines
    QCOMPARE(f.sel.selections(), (SelectionList{{4, 4}, {9, 9}, {14, 14}, {17, 17}, {22, 22}}));
    QCOMPARE(f.sel.primaryIndex(), 4);
    f.key(Qt::Key_Down, mods); // last row: nothing to add
    QCOMPARE(f.sel.count(), 5);
    f.type(u"X"_s);
    QCOMPARE(f.text(), u"abcdXef\nabX\nabcdXef\nX\nabcdXef"_s);
    QCOMPARE(f.doc.undoStack().undoSteps(), 1);

    f.key(Qt::Key_Escape);
    QCOMPARE(f.sel.count(), 1);
    QCOMPARE(f.sel.primary(), (Selection{f.sel.primary().head, f.sel.primary().head}));
    QVERIFY(!f.key(Qt::Key_Escape)); // a lone cursor leaves Escape alone
  }

  void addCursorAboveStartsFromTopmost() {
    Fixture f(u"aaaa\nbbbb\ncccc\ndddd"_s);
    f.sel.set({{7, 7}, {12, 12}}); // rows 1 and 2
    const auto mods = Qt::ControlModifier | Qt::AltModifier;
    f.key(Qt::Key_Up, mods);
    QCOMPARE(f.sel.selections(), (SelectionList{{2, 2}, {7, 7}, {12, 12}}));
    QCOMPARE(f.sel.primaryIndex(), 0);
    QVERIFY(f.key(Qt::Key_Up, mods)); // consumed, but already on the first row
    QCOMPARE(f.sel.count(), 3);
  }

  void addCursorSkipsFoldedLinesAndFollowsWrap() {
    Fixture f(u"a\nb\nc\nd"_s);
    f.map.fold(0, 2);
    f.sel.setSingle(0);
    f.key(Qt::Key_Down, Qt::ControlModifier | Qt::AltModifier);
    QCOMPARE(f.sel.selections(), (SelectionList{{0, 0}, {6, 6}})); // rows: line 0 (folded 1..2), line 3
  }

  void addNextOccurrenceSelectsWordThenMatches() {
    Fixture f(u"foo bar foo foobar foo"_s);
    f.sel.setSingle(1);
    QVERIFY(f.key(Qt::Key_D, Qt::ControlModifier));
    QCOMPARE(f.sel.selections(), (SelectionList{{0, 3}}));
    QVERIFY(f.key(Qt::Key_D, Qt::ControlModifier));
    QCOMPARE(f.sel.selections(), (SelectionList{{0, 3}, {8, 11}})); // whole words only: not foobar
    QCOMPARE(f.sel.primary(), (Selection{8, 11}));
    QVERIFY(f.key(Qt::Key_D, Qt::ControlModifier));
    QCOMPARE(f.sel.selections(), (SelectionList{{0, 3}, {8, 11}, {19, 22}}));
    QVERIFY(!f.key(Qt::Key_D, Qt::ControlModifier) || f.sel.count() == 3); // none left
    f.type(u"X"_s);
    QCOMPARE(f.text(), u"X bar X foobar X"_s);
    QCOMPARE(f.doc.undoStack().undoSteps(), 1);
  }

  void addNextOccurrenceWrapsAndSubstringsWhenSelectionIsPartial() {
    Fixture f(u"foo foobar foo"_s);
    f.sel.setSingle(4, 7); // "foo" inside "foobar": a substring search
    f.key(Qt::Key_D, Qt::ControlModifier);
    QCOMPARE(f.sel.selections(), (SelectionList{{4, 7}, {11, 14}}));
    f.key(Qt::Key_D, Qt::ControlModifier);
    QCOMPARE(f.sel.selections(), (SelectionList{{0, 3}, {4, 7}, {11, 14}})); // wrapped to the start
    QCOMPARE(f.sel.primaryIndex(), 0);
  }

  void selectAllOccurrences() {
    Fixture f(u"a ab a abab a"_s);
    f.sel.setSingle(0);
    f.key(Qt::Key_L, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(f.sel.selections(), (SelectionList{{0, 1}, {5, 6}, {12, 13}}));
    f.sel.setSingle(7, 9); // "ab" inside "abab": not a whole word, so substrings count
    f.key(Qt::Key_L, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(f.sel.selections(), (SelectionList{{2, 4}, {7, 11}})); // touching matches merge
    QCOMPARE(f.sel.primary(), (Selection{7, 11}));

    Fixture g(u"x x x x x"_s);
    g.ctx.settings.maxSelections = 3;
    g.sel.setSingle(0);
    bool capped = false;
    QVERIFY(commands::selectAllOccurrences(g.ctx, &capped));
    QVERIFY(capped);
    QCOMPARE(g.sel.count(), 3);
  }
};

QTEST_MAIN(TstInput)
#include "tst_input.moc"
