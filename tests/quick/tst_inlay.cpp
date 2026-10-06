#include <QtGui/QGuiApplication>
#include <QtQml/QQmlEngine>
#include <QtQml/qqmlextensionplugin.h>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickView>
#include <QtQuick/QQuickWindow>
#include <QtQuick/qsgrendererinterface.h>
#include <QtTest>

#include "core/inlayhints.h"
#include "quick/codeeditor.h"

Q_IMPORT_QML_PLUGIN(me_blq_qmlcodeeditorPlugin)

namespace {

QVariantMap hint(int line, int character, const QVariant &label, int kind = 1, bool padLeft = false, bool padRight = false) {
  return {{"position", QVariantMap{{"line", line}, {"character", character}}}, {"label", label}, {"kind", kind},
          {"paddingLeft", padLeft}, {"paddingRight", padRight}};
}

bool hasInk(const QImage &image, const QRect &rect, const QColor &bg) {
  for (int y = rect.top(); y < rect.bottom() && y < image.height(); ++y)
    for (int x = rect.left(); x < rect.right() && x < image.width(); ++x)
      if (image.pixelColor(x, y) != bg)
        return true;
  return false;
}

} // namespace

class TstInlay : public QObject {
  Q_OBJECT
  QQmlEngine m_engine;

  struct Shown {
    std::unique_ptr<QQuickView> view;
    CodeEditor *editor = nullptr;
  };
  Shown showEditor(int height = 200, int width = 400) {
    Shown shown;
    shown.view = std::make_unique<QQuickView>(&m_engine, nullptr);
    shown.view->setSource(QUrl::fromLocalFile(QFINDTESTDATA("editor.qml")));
    shown.editor = qobject_cast<CodeEditor *>(shown.view->rootObject());
    shown.editor->setWidth(width);
    shown.editor->setHeight(height);
    shown.view->resize(width, height);
    shown.view->show();
    if (!QTest::qWaitForWindowExposed(shown.view.get()))
      return {};
    shown.editor->forceActiveFocus();
    shown.editor->setCursorBlinkInterval(0);
    return shown;
  }
  // The x of the cursor cell at `offset`, in item coordinates.
  static qreal xAt(CodeEditor *editor, qsizetype offset) { return editor->rectForPosition(offset).x(); }

private slots:
  void initTestCase() { QQuickWindow::setGraphicsApi(QSGRendererInterface::Software); }

  void hintsAreLaidOutInlineAndPushTheTextAfterThemRight() {
    auto [view, editor] = showEditor();
    editor->setText(QStringLiteral("let x = 5;"));
    const qreal cell = editor->metrics().cellAdvance();
    const qreal before = xAt(editor, 8); // the '5'
    QCOMPARE(before, 8 * cell);
    editor->setInlayHints(QVariantList{hint(0, 5, ": number")}); // " : number " = 10 cells (padding included)
    QCOMPARE(editor->inlayHintCount(), 1);
    QCOMPARE(xAt(editor, 8), before + 10 * cell);
    QCOMPARE(xAt(editor, 4), 4 * cell); // text before the hint is where it was
    // The text itself is untouched.
    QCOMPARE(editor->document()->rope().toString(0, editor->document()->length()), QStringLiteral("let x = 5;"));
    // The line is wider by the hint.
    QTRY_VERIFY(editor->contentWidth() >= 20 * cell);
    editor->clearInlayHints();
    QCOMPARE(editor->inlayHintCount(), 0);
    QCOMPARE(xAt(editor, 8), before);
  }

  void theCursorSitsOnTheSideTheHintLeansAway() {
    auto [view, editor] = showEditor();
    editor->setText(QStringLiteral("foo(bar, baz)"));
    const qreal cell = editor->metrics().cellAdvance();
    // A parameter hint leans on the text after it: a cursor at its position is after the hint.
    editor->setInlayHints(QVariantList{hint(0, 4, "name:", 2)}); // " name: " = 7 cells
    QCOMPARE(xAt(editor, 3), 3 * cell);
    QCOMPARE(xAt(editor, 4), 4 * cell + 7 * cell);
    // A type hint leans on the text before it: a cursor at its position is before the hint.
    editor->setInlayHints(QVariantList{hint(0, 3, ": T", 1)}); // " : T " = 5 cells
    QCOMPARE(xAt(editor, 3), 3 * cell);
    QCOMPARE(xAt(editor, 4), 4 * cell + 5 * cell);
    // So walking right over the hint takes no keystroke of its own: columns are the text's.
    editor->setCursorPosition(2);
    QTest::keyClick(view.get(), Qt::Key_Right);
    QCOMPARE(editor->cursorPosition(), 3);
    QTest::keyClick(view.get(), Qt::Key_Right);
    QCOMPARE(editor->cursorPosition(), 4);
    QTest::keyClick(view.get(), Qt::Key_Left);
    QCOMPARE(editor->cursorPosition(), 3);
  }

  void clicksOnAHintLandNextToItAndClicksAfterItHitTheText() {
    auto [view, editor] = showEditor();
    editor->setText(QStringLiteral("let x = 5;"));
    editor->setInlayHints(QVariantList{hint(0, 5, ": number")});
    const qreal cell = editor->metrics().cellAdvance();
    const qreal y = editor->metrics().lineHeight() / 2;
    // The hint covers cells 5..14.
    const qsizetype inside = editor->positionAt(9.4 * cell, y);
    QCOMPARE(inside, 5);
    QCOMPARE(editor->positionAt(5.2 * cell, y), 5);
    QCOMPARE(editor->positionAt(14.8 * cell, y), 5);
    // After it: "let x" is 5 cells, the hint 10, then " = 5;".
    QCOMPARE(editor->positionAt(15.6 * cell, y), 6);      // the space after x
    QCOMPARE(editor->positionAt(18.2 * cell, y), 8);      // the '5'
    QCOMPARE(editor->positionAt(40 * cell, y), 10);       // past the end
  }

  void selectionsSpanTheHintsInsideThem() {
    auto [view, editor] = showEditor();
    editor->setText(QStringLiteral("aa bb cc\nsecond"));
    editor->setInlayHints(QVariantList{hint(0, 3, ": T")});
    editor->select(0, 8);
    QCOMPARE(editor->selectionEnd() - editor->selectionStart(), 8);
    const QImage image = view->grabWindow();
    // The selection color is drawn under the hint too.
    const qreal cell = editor->metrics().cellAdvance();
    const int y = int(editor->metrics().lineHeight() / 2);
    const QColor selection = editor->theme()->selection();
    int hits = 0;
    for (int x = int(3.2 * cell); x < int(7 * cell); ++x)
      hits += image.pixelColor(x, y) == selection;
    QVERIFY(hits > 0);
  }

  void verticalMovementUsesTheShownPosition() {
    auto [view, editor] = showEditor();
    editor->setText(QStringLiteral("aaaaaaaaaa\nbbbbbbbbbb"));
    editor->setInlayHints(QVariantList{hint(0, 2, "xx")}); // " xx " = 4 cells on line 0 only
    editor->setCursorPosition(6);                          // shown at 6 + 4 = 10 cells
    QTest::keyClick(view.get(), Qt::Key_Down);
    QCOMPARE(editor->cursorLine(), 1);
    QCOMPARE(editor->cursorColumn(), 10); // the same x on a line without the hint (clamped to its end)
    QTest::keyClick(view.get(), Qt::Key_Up);
    QCOMPARE(editor->cursorLine(), 0);
    QCOMPARE(editor->cursorColumn(), 6); // and back to where it was
  }

  void hintsFollowEditsAndStayWithTheirText() {
    auto [view, editor] = showEditor();
    editor->setText(QStringLiteral("let x = 5;"));
    editor->setInlayHints(QVariantList{hint(0, 5, ": number", 1), hint(0, 8, "n:", 2)});
    const qreal cell = editor->metrics().cellAdvance();
    editor->document()->insert(0, QStringLiteral("// c\n"));
    // Now on line 1; the type hint is still after "x", the parameter hint still before the 5.
    const qsizetype lineStart = editor->document()->rope().lineStart(1);
    // From before the type hint to after the parameter hint: the hint (10), " = " (3), " n: " (4).
    QCOMPARE(xAt(editor, lineStart + 8) - xAt(editor, lineStart + 5), 10 * cell + 3 * cell + 4 * cell);
    // Typing right at a type hint's position goes before it (the identifier grows), at a parameter
    // hint's position after it.
    editor->setCursorPosition(lineStart + 5);
    editor->insert(QStringLiteral("y"));
    QCOMPARE(xAt(editor, lineStart + 6), 6 * cell); // cursor is still before the type hint, after the 'y'
    QCOMPARE(xAt(editor, lineStart + 7), 7 * cell + 10 * cell);
  }

  void hintsDrawAPillBehindTheLabel() {
    auto [view, editor] = showEditor();
    editor->setText(QStringLiteral("first\nlet x = 5;"));
    editor->setCursorPosition(0);
    editor->setInlayHints(QVariantList{hint(1, 5, ": number")});
    const qreal cell = editor->metrics().cellAdvance();
    const int lh = int(editor->metrics().lineHeight());
    const QColor bg = editor->theme()->background();
    QTRY_VERIFY(hasInk(view->grabWindow(), QRect(int(5 * cell), lh, int(10 * cell), lh), bg));
    // Nothing on the line above.
    QVERIFY(!hasInk(view->grabWindow(), QRect(int(6 * cell), lh * 3, int(10 * cell), lh), bg));
  }

  void hintsWidenWrappedLinesAndStayWithTheirCharacter() {
    auto [view, editor] = showEditor(300, 400);
    editor->setText(QString(30, u'x'));
    editor->setWrapMode(CodeEditor::WrapAtColumn);
    editor->setWrapColumn(20);
    QTRY_COMPARE(editor->displayMap().rowCountOfLine(0), 2);
    QCOMPARE(editor->displayMap().rowAt(1).startColumn, 20);
    // " parameter-name: " is 17 cells and leans on the character after it, which becomes 18 wide:
    // nineteen characters fit the first row, the hinted one starts the second.
    editor->setInlayHints(QVariantList{hint(0, 19, "parameter-name:", 2)});
    QCOMPARE(editor->displayMap().rowCountOfLine(0), 3);
    QCOMPARE(editor->displayMap().rowAt(1).startColumn, 19);
    QCOMPARE(editor->displayMap().rowAt(1).endColumn, 22); // 18 + 1 + 1 cells, the next would make 21
    QCOMPARE(editor->displayMap().rowAt(2).startColumn, 22);
    QCOMPARE(editor->displayMap().rowAt(2).endColumn, 30);
    // The row shows the hint in front of its first character.
    const qreal cell = editor->metrics().cellAdvance();
    const qsizetype rowStart = editor->document()->rope().offsetAt({0, 19});
    QCOMPARE(xAt(editor, rowStart), 17 * cell);                // after the hint
    QCOMPARE(xAt(editor, rowStart + 1), 18 * cell);
    editor->clearInlayHints();
    QCOMPARE(editor->displayMap().rowCountOfLine(0), 2);
    QCOMPARE(editor->displayMap().rowAt(1).startColumn, 20);
  }

  void aHintThatLeansBackStaysOnTheRowOfItsCharacter() {
    auto [view, editor] = showEditor(300, 400);
    editor->setText(QString(30, u'x'));
    editor->setWrapMode(CodeEditor::WrapAtColumn);
    editor->setWrapColumn(20);
    QTRY_COMPARE(editor->displayMap().rowCountOfLine(0), 2);
    // A type hint after the 20th character (so at column 20, the start of row two) belongs to the
    // character before it, which ends row one: row one grows past the column and the hint goes with
    // its character, so the break moves up to before that character.
    editor->setInlayHints(QVariantList{hint(0, 20, "number-type", 1)}); // " number-type " = 13 cells
    QCOMPARE(editor->displayMap().rowAt(1).startColumn, 19);
    const qreal cell = editor->metrics().cellAdvance();
    // The hint is drawn after the character it leans on, which now starts row two; a cursor at its
    // position is before it (one cell in) and the next character is after it.
    QCOMPARE(xAt(editor, editor->document()->rope().offsetAt({0, 19})), 0.0);
    QCOMPARE(xAt(editor, editor->document()->rope().offsetAt({0, 20})), 1 * cell);
    QCOMPARE(xAt(editor, editor->document()->rope().offsetAt({0, 21})), 1 * cell + 13 * cell + 1 * cell);
  }

  void wrapRecomputesWhenTheViewIsNarrowedAfterwards() {
    auto [view, editor] = showEditor(300, 400);
    editor->setText(QString(60, u'x'));
    editor->setInlayHints(QVariantList{hint(0, 10, "label", 2)});
    editor->setWrapMode(CodeEditor::WrapAtViewport);
    QTRY_VERIFY(editor->displayMap().rowCountOfLine(0) >= 2);
    const qsizetype with = editor->displayMap().rowCountOfLine(0);
    editor->clearInlayHints();
    QVERIFY(editor->displayMap().rowCountOfLine(0) <= with);
  }

  void lspLabelPartsPaddingAndClamping() {
    auto [view, editor] = showEditor();
    editor->setText(QStringLiteral("ab\ncd"));
    editor->setInlayHints(QVariantList{
      hint(0, 1, QVariantList{QVariantMap{{"value", "x"}}, QVariantMap{{"value", ": y"}}}, 1, true, true),
      hint(9, 9, "far", 1), // past the text: clamped to its end
      hint(1, 0, "")        // an empty label shows nothing
    });
    QCOMPARE(editor->inlayHintCount(), 2);
    const qreal cell = editor->metrics().cellAdvance();
    // Padding on both sides: two spaces each side around "x: y" = 8 cells.
    QCOMPARE(xAt(editor, 2), 2 * cell + 8 * cell);
  }

  void inlayHintSpecsMapKindsToGravity() {
    const qce::Rope rope = qce::Rope::fromString(QStringLiteral("abc\ndef"));
    qce::InlayHint typeHint, paramHint;
    typeHint.position = {0, 1};
    typeHint.label = QStringLiteral("T");
    typeHint.kind = qce::TypeHint;
    paramHint = typeHint;
    paramHint.kind = qce::ParameterHint;
    paramHint.position = {1, 2};
    const QList<qce::DecorationSpec> specs = qce::inlayHintSpecs({typeHint, paramHint}, rope);
    QCOMPARE(specs.size(), 2);
    QCOMPARE(specs[0].start, 1);
    QCOMPARE(specs[0].startGravity, qce::Gravity::Right);
    QCOMPARE(specs[1].start, 6);
    QCOMPARE(specs[1].startGravity, qce::Gravity::Left);
    QCOMPARE(specs[0].text, QStringLiteral(" T "));
    QCOMPARE(specs[0].kind, qce::DecorationKind::InlineText);
  }
};

QTEST_MAIN(TstInlay)
#include "tst_inlay.moc"
