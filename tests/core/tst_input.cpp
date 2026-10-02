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
};

struct Fixture {
  TextDocument doc;
  SelectionSet sel{&doc};
  DisplayMap map{&doc};
  GridCursorLayout layout{&doc, 4, 3};
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
};

QTEST_MAIN(TstInput)
#include "tst_input.moc"
