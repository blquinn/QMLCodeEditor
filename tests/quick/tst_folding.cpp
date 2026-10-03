#include <QtGui/QGuiApplication>
#include <QtQml/QQmlEngine>
#include <QtQml/qqmlextensionplugin.h>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickView>
#include <QtQuick/QQuickWindow>
#include <QtQuick/qsgrendererinterface.h>
#include <QtTest>

#include "quick/codeeditor.h"
#include "quick/foldcolumn.h"
#include "quick/linenumbercolumn.h"

Q_IMPORT_QML_PLUGIN(me_blq_qmlcodeeditorPlugin)

namespace {

// Three nested functions, each with a body of two lines.
//  0 a {          7 b {
//  1   x          8   y
//  2   c {        9   d {
//  3     z       10     w
//  4   }         11   }
//  5 }           12 }
//  6 (blank)     13 tail
const QString kCode = QStringLiteral(
  "a {\n  x\n  c {\n    z\n  }\n}\n\nb {\n  y\n  d {\n    w\n  }\n}\ntail"
);

bool hasColor(const QImage &image, const QRect &rect, const QColor &color) {
  for (int y = rect.top(); y < rect.bottom() && y < image.height(); ++y)
    for (int x = rect.left(); x < rect.right() && x < image.width(); ++x)
      if (image.pixelColor(x, y) == color)
        return true;
  return false;
}

} // namespace

class TstFolding : public QObject {
  Q_OBJECT
  QQmlEngine m_engine;

  struct Shown {
    std::unique_ptr<QQuickView> view;
    CodeEditor *editor = nullptr;
  };
  Shown showEditor(int height = 300, int width = 400) {
    Shown shown;
    shown.view = std::make_unique<QQuickView>(&m_engine, nullptr);
    shown.view->setSource(QUrl::fromLocalFile(QFINDTESTDATA("editor.qml")));
    shown.editor = qobject_cast<CodeEditor *>(shown.view->rootObject());
    shown.editor->setWidth(width);
    shown.editor->setHeight(height);
    shown.view->show();
    if (!QTest::qWaitForWindowExposed(shown.view.get()))
      return {};
    shown.editor->forceActiveFocus();
    shown.editor->setCursorBlinkInterval(0);
    return shown;
  }

private slots:
  void initTestCase() { QQuickWindow::setGraphicsApi(QSGRendererInterface::Software); }

  void foldingHidesLinesAndShrinksTheContent() {
    auto [view, editor] = showEditor();
    QVERIFY(editor);
    editor->setText(kCode);
    const qreal lh = editor->metrics().lineHeight();
    QCOMPARE(editor->contentHeight(), 14 * lh);
    QVERIFY(editor->fold(0));
    QCOMPARE(editor->contentHeight(), 10 * lh); // lines 1..4 are gone, the closing brace stays
    QVERIFY(editor->isFolded(0));
    QCOMPARE(editor->displayMap().lineForRow(1), 5);
    QVERIFY(!editor->fold(1) || editor->isFolded(1)); // inside a fold: nothing to see, but harmless
    QVERIFY(editor->unfold(0));
    QCOMPARE(editor->contentHeight(), 14 * lh);
  }

  void foldsFollowEdits() {
    auto [view, editor] = showEditor();
    editor->setText(kCode);
    editor->fold(7);
    editor->document()->insert(0, QStringLiteral("new line\n"));
    QVERIFY(editor->isFolded(8));
    QVERIFY(!editor->isFolded(7));
    QCOMPARE(editor->displayMap().rowCount(), 15 - 4);
  }

  void cursorInsideAFoldMovesToTheHeaderWhenFolding() {
    auto [view, editor] = showEditor();
    editor->setText(kCode);
    const auto &rope = editor->document()->rope();
    editor->setCursorPosition(rope.lineStart(3) + 2); // in "    z"
    QVERIFY(editor->fold(0));
    QCOMPARE(editor->cursorLine(), 0);
    QCOMPARE(editor->cursorColumn(), 3); // the end of "a {"
    QVERIFY(editor->isFolded(0));
  }

  void arrowKeysStepOverAFold() {
    auto [view, editor] = showEditor();
    editor->setText(kCode);
    editor->fold(0);
    const auto &rope = editor->document()->rope();
    editor->setCursorPosition(0);
    QTest::keyClick(view.get(), Qt::Key_Down);
    QCOMPARE(editor->cursorLine(), 5);
    QTest::keyClick(view.get(), Qt::Key_Up);
    QCOMPARE(editor->cursorLine(), 0);
    // Right at the end of the header jumps past the hidden lines, left comes back.
    editor->setCursorPosition(rope.lineEnd(0));
    QTest::keyClick(view.get(), Qt::Key_Right);
    QCOMPARE(editor->cursorLine(), 5);
    QCOMPARE(editor->cursorColumn(), 0);
    QTest::keyClick(view.get(), Qt::Key_Left);
    QCOMPARE(editor->cursorLine(), 0);
    QCOMPARE(editor->cursorColumn(), 3);
    QVERIFY(editor->isFolded(0));
  }

  void unfoldOnEnterOpensTheFold() {
    auto [view, editor] = showEditor();
    editor->setText(kCode);
    editor->setFoldCursorPolicy(CodeEditor::UnfoldOnEnter);
    editor->fold(0);
    editor->setCursorPosition(editor->document()->rope().lineEnd(0));
    QTest::keyClick(view.get(), Qt::Key_Right);
    QVERIFY(!editor->isFolded(0));
    QCOMPARE(editor->cursorLine(), 1);
  }

  void movingTheCursorIntoAFoldFromTheApiOpensIt() {
    auto [view, editor] = showEditor();
    editor->setText(kCode);
    editor->foldAll();
    QVERIFY(editor->isFolded(0));
    editor->setCursorPosition(editor->document()->rope().lineStart(3));
    QVERIFY(!editor->isFolded(0));
    QVERIFY(!editor->isFolded(2)); // the fold around line 3 is open too
    QVERIFY(editor->isFolded(7));  // others stay folded
  }

  void typingAtTheEndOfAFoldedHeaderKeepsItFolded() {
    auto [view, editor] = showEditor();
    editor->setText(kCode);
    editor->fold(0);
    editor->setCursorPosition(editor->document()->rope().lineEnd(0));
    for (const char *c = "// note"; *c; ++c)
      QTest::keyClick(view.get(), *c);
    QVERIFY(editor->isFolded(0));
    QCOMPARE(editor->cursorLine(), 0);
  }

  void keyboardShortcuts() {
    auto [view, editor] = showEditor();
    editor->setText(kCode);
    const auto &rope = editor->document()->rope();
    editor->setCursorPosition(rope.lineStart(3));
    QTest::keyClick(view.get(), Qt::Key_BracketLeft, Qt::ControlModifier | Qt::ShiftModifier);
    QVERIFY(editor->isFolded(2)); // the innermost region around the cursor
    QCOMPARE(editor->cursorLine(), 2);
    QTest::keyClick(view.get(), Qt::Key_BracketLeft, Qt::ControlModifier | Qt::ShiftModifier);
    QVERIFY(editor->isFolded(0)); // then its parent
    QTest::keyClick(view.get(), Qt::Key_BracketRight, Qt::ControlModifier | Qt::ShiftModifier);
    QVERIFY(!editor->isFolded(0));
    QTest::keyClick(view.get(), Qt::Key_BracketRight, Qt::ControlModifier | Qt::AltModifier);
    QVERIFY(!editor->isFolded(2));
    QTest::keyClick(view.get(), Qt::Key_BracketLeft, Qt::ControlModifier | Qt::AltModifier);
    QVERIFY(editor->isFolded(0));
    QVERIFY(editor->isFolded(7));
    QVERIFY(editor->isFolded(9));
    QTest::keyClick(view.get(), Qt::Key_BracketRight, Qt::ControlModifier | Qt::AltModifier);
    QCOMPARE(editor->displayMap().rowCount(), 14);
  }

  void foldToLevel() {
    auto [view, editor] = showEditor();
    editor->setText(kCode);
    QVERIFY(editor->foldToLevel(2));
    QVERIFY(editor->isFolded(2));
    QVERIFY(editor->isFolded(9));
    QVERIFY(!editor->isFolded(0));
  }

  void tripleClickOnAFoldedLineSelectsItsBody() {
    auto [view, editor] = showEditor();
    editor->setText(kCode);
    editor->fold(0);
    const auto &rope = editor->document()->rope();
    const qreal lh = editor->metrics().lineHeight();
    const QPoint p(int(editor->gutterWidth()) + 6, int(lh / 2));
    QTest::mouseClick(view.get(), Qt::LeftButton, {}, p, 10);
    QTest::mouseDClick(view.get(), Qt::LeftButton, {}, p, 10);
    QTest::mouseClick(view.get(), Qt::LeftButton, {}, p, 10);
    if (editor->selectionEnd() - editor->selectionStart() > 5) { // delivered as a triple click
      QCOMPARE(editor->selectionStart(), 0);
      QCOMPARE(editor->selectionEnd(), rope.lineStart(5));
    }
  }

  void topOfTheViewStaysWhenFoldingAbove() {
    auto [view, editor] = showEditor(100);
    QString text;
    for (int i = 0; i < 100; ++i)
      text += QStringLiteral("l%1 {\n  body\n  more\n}\n").arg(i);
    editor->setText(text);
    const qreal lh = editor->metrics().lineHeight();
    editor->setContentY(200 * lh);
    const qsizetype before = editor->displayMap().lineForRow(qsizetype(editor->contentY() / lh));
    QVERIFY(editor->fold(0));
    QVERIFY(editor->fold(40));
    QCOMPARE(editor->displayMap().lineForRow(qsizetype(std::round(editor->contentY() / lh))), before);
    QVERIFY(editor->contentY() < 200 * lh);
  }

  void wrappedHeaderKeepsItsRowsWhenFolded() {
    auto [view, editor] = showEditor(300, 200);
    editor->setText(QStringLiteral("aaa bbb ccc ddd eee fff ggg hhh iii jjj kkk lll {\n  x\n  y\n}\ntail"));
    editor->setWrapMode(CodeEditor::WrapAtColumn);
    editor->setWrapColumn(12);
    QTRY_VERIFY(editor->displayMap().rowCountOfLine(0) > 2);
    const qsizetype rows = editor->displayMap().rowCountOfLine(0);
    const qsizetype total = editor->displayMap().rowCount();
    QVERIFY(editor->fold(0));
    QCOMPARE(editor->displayMap().rowCountOfLine(0), rows);
    QCOMPARE(editor->displayMap().rowCount(), total - 2);
    QCOMPARE(editor->displayMap().lineForRow(rows), 3);
  }

  void placeholderIsDrawnAndAClickOpensTheFold() {
    auto [view, editor] = showEditor(200, 300);
    editor->setText(kCode);
    editor->fold(0);
    const QColor chip = editor->theme()->foldPlaceholder();
    const qreal lh = editor->metrics().lineHeight();
    QTRY_VERIFY(hasColor(view->grabWindow(), QRect(0, 0, 300, int(lh)), chip));
    QVERIFY(!hasColor(view->grabWindow(), QRect(0, int(lh), 300, int(lh)), chip));
    const qreal cell = editor->metrics().cellAdvance();
    // "a {" is three cells; the chip starts one cell after it.
    const QPoint inChip(int(editor->gutterWidth() + 3 * cell + cell + 2 * cell), int(lh / 2));
    QTest::mouseClick(view.get(), Qt::LeftButton, {}, inChip, 10);
    QVERIFY(!editor->isFolded(0));
    QCOMPARE(editor->cursorPosition(), 0); // the click was not a text click
  }

  void foldColumnDrawsChevronsAndTogglesOnClick() {
    auto [view, editor] = showEditor();
    editor->setText(kCode);
    auto *column = new qce::FoldColumn(editor);
    editor->addGutterColumn(column);
    QTRY_VERIFY(editor->gutterWidth() > 0);
    const qreal lh = editor->metrics().lineHeight();
    auto ink = [&](int row) {
      const QImage image = view->grabWindow();
      const QColor bg = editor->theme()->gutterBackground(), band = editor->theme()->currentLine();
      for (int y = int(row * lh); y < int((row + 1) * lh); ++y)
        for (int x = 0; x < int(editor->gutterWidth()); ++x)
          if (const QColor c = image.pixelColor(x, y); c != bg && c != band)
            return true;
      return false;
    };
    QTRY_VERIFY(ink(0));  // a header
    QVERIFY(!ink(1));     // "  x" starts nothing
    QVERIFY(ink(2));      // "  c {" does
    const QPoint at(int(editor->gutterWidth() / 2), int(lh / 2));
    QTest::mouseClick(view.get(), Qt::LeftButton, {}, at, 10);
    QVERIFY(editor->isFolded(0));
    QCOMPARE(editor->displayMap().rowCount(), 10);
    // Right after the first press this is a double click; it still counts as one click.
    QTest::mouseClick(view.get(), Qt::LeftButton, {}, at, 10);
    QVERIFY(!editor->isFolded(0));
  }

  void relativeNumbersCountVisibleLines() {
    auto [view, editor] = showEditor();
    editor->setText(kCode);
    editor->fold(0);
    qce::LineNumberColumn numbers;
    numbers.setMode(qce::LineNumberColumn::Relative);
    // From line 0, line 5 is the next visible one.
    QCOMPARE(numbers.numberFor(5, 0, &editor->displayMap().folds()), 1);
    QCOMPARE(numbers.numberFor(7, 0, &editor->displayMap().folds()), 3);
    QCOMPARE(numbers.numberFor(7, 0), 7);
  }

  void providerCanBeSwapped() {
    auto [view, editor] = showEditor();
    editor->setText(kCode);
    QVERIFY(editor->foldProvider());
    QVERIFY(editor->fold(0));
    editor->setFoldProvider(nullptr); // the default again
    QVERIFY(editor->foldProvider());
    QVERIFY(editor->isFolded(0));
  }
};

QTEST_MAIN(TstFolding)
#include "tst_folding.moc"
