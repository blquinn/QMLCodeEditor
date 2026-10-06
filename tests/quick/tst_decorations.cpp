#include <QtGui/QGuiApplication>
#include <QtQml/QQmlEngine>
#include <QtQml/qqmlextensionplugin.h>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickView>
#include <QtQuick/QQuickWindow>
#include <QtQuick/qsgrendererinterface.h>
#include <QtTest>

#include "quick/codeeditor.h"
#include "quick/decorationcolumn.h"

Q_IMPORT_QML_PLUGIN(me_blq_qmlcodeeditorPlugin)

namespace {

const QColor kRed(0xff, 0x00, 0x00);

bool hasColor(const QImage &image, const QRect &rect, const QColor &color) {
  for (int y = rect.top(); y < rect.bottom() && y < image.height(); ++y)
    for (int x = rect.left(); x < rect.right() && x < image.width(); ++x)
      if (image.pixelColor(x, y) == color)
        return true;
  return false;
}

// Any pixel in `rect` that is neither of the two background colors.
bool hasInk(const QImage &image, const QRect &rect, const QColor &bg, const QColor &band) {
  for (int y = rect.top(); y < rect.bottom() && y < image.height(); ++y)
    for (int x = rect.left(); x < rect.right() && x < image.width(); ++x)
      if (const QColor c = image.pixelColor(x, y); c != bg && c != band)
        return true;
  return false;
}

// Three nested blocks: lines 1..4 fold under line 0.
const QString kCode = QStringLiteral("a {\n  x\n  c {\n    z\n  }\n}\ntail");

} // namespace

class TstDecorations : public QObject {
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
  static QRect rowRect(CodeEditor *editor, int row, int width = 400) {
    const int lh = int(editor->metrics().lineHeight());
    return QRect(int(editor->gutterWidth()), row * lh, width, lh);
  }

private slots:
  void initTestCase() { QQuickWindow::setGraphicsApi(QSGRendererInterface::Software); }

  void optionsBecomeADecoration() {
    auto [view, editor] = showEditor();
    editor->setText(QStringLiteral("alpha\nbeta"));
    const int id = editor->addDecoration(
      1, 4,
      {{"kind", int(CodeEditor::Squiggle)}, {"color", kRed}, {"severity", 2}, {"priority", 3}, {"layer", 5},
       {"text", "hi"}, {"endGravity", "right"}}
    );
    QVERIFY(id > 0);
    const qce::Decoration d = editor->decorations()->decoration(id);
    QCOMPARE(d.kind, qce::DecorationKind::Squiggle);
    QCOMPARE(d.color, kRed);
    QCOMPARE(d.severity, 2);
    QCOMPARE(d.priority, 3);
    QCOMPARE(d.layer, 5);
    QCOMPARE(d.text, QStringLiteral("hi"));
    QCOMPARE(d.start, 1);
    QCOMPARE(d.end, 4);
    editor->document()->insert(4, QStringLiteral("!")); // the end leans right: the text is inside
    QCOMPARE(editor->decorations()->decoration(id).end, 5);
    QCOMPARE(editor->addDecoration(0, 1, {{"kind", 99}}), 0);
    editor->clearDecorations(4); // another layer: nothing happens
    QVERIFY(editor->decorations()->contains(id));
    editor->clearDecorations(5);
    QVERIFY(!editor->decorations()->contains(id));
    const int plain = editor->addDecoration(0, 1);
    QVERIFY(editor->removeDecoration(plain));
    QVERIFY(!editor->removeDecoration(plain));
  }

  void backgroundIsDrawnBehindItsRowsOnly() {
    auto [view, editor] = showEditor();
    editor->setText(QStringLiteral("alpha\nbeta\ngamma"));
    editor->addDecoration(0, 5, {{"kind", int(CodeEditor::Background)}, {"color", kRed}});
    QTRY_VERIFY(hasColor(view->grabWindow(), rowRect(editor, 0), kRed));
    const QImage image = view->grabWindow();
    QVERIFY(!hasColor(image, rowRect(editor, 1), kRed));
    QVERIFY(!hasColor(image, rowRect(editor, 2), kRed));
    // And only as wide as the range: "alpha" is five cells.
    const int right = int(editor->gutterWidth() + 5 * editor->metrics().cellAdvance()) + 2;
    QVERIFY(!hasColor(image, QRect(right, 0, 100, int(editor->metrics().lineHeight())), kRed));
  }

  void underlineIsDrawn() {
    auto [view, editor] = showEditor();
    editor->setText(QStringLiteral("alpha\nbeta"));
    editor->addDecoration(6, 10, {{"kind", int(CodeEditor::Underline)}, {"color", kRed}});
    QTRY_VERIFY(hasColor(view->grabWindow(), rowRect(editor, 1), kRed));
    QVERIFY(!hasColor(view->grabWindow(), rowRect(editor, 0), kRed));
  }

  void decorationsFollowEdits() {
    auto [view, editor] = showEditor();
    editor->setText(QStringLiteral("alpha\nbeta"));
    editor->addDecoration(6, 10, {{"kind", int(CodeEditor::Background)}, {"color", kRed}});
    QTRY_VERIFY(hasColor(view->grabWindow(), rowRect(editor, 1), kRed));
    editor->document()->insert(0, QStringLiteral("new\n"));
    QTRY_VERIFY(hasColor(view->grabWindow(), rowRect(editor, 2), kRed));
    QVERIFY(!hasColor(view->grabWindow(), rowRect(editor, 1), kRed));
  }

  void wrappedLinesGetASpanPerRow() {
    auto [view, editor] = showEditor(300, 200);
    editor->setText(QString(60, u'x') + u'\n' + QStringLiteral("tail"));
    editor->setWrapMode(CodeEditor::WrapAtViewport);
    editor->addDecoration(5, 55, {{"kind", int(CodeEditor::Background)}, {"color", kRed}});
    QTRY_VERIFY(editor->displayMap().rowCount() > 3);
    const int rows = int(editor->displayMap().rowCountOfLine(0));
    QVERIFY(rows >= 3);
    QTRY_VERIFY(hasColor(view->grabWindow(), rowRect(editor, 0, 200), kRed));
    const QImage image = view->grabWindow();
    for (int row = 0; row < rows; ++row)
      QVERIFY2(hasColor(image, rowRect(editor, row, 200), kRed), qPrintable(QStringLiteral("row %1").arg(row)));
    QVERIFY(!hasColor(image, rowRect(editor, rows, 200), kRed)); // "tail"
  }

  void foldedLinesDrawNothing() {
    auto [view, editor] = showEditor();
    editor->setText(kCode);
    const auto &rope = editor->document()->rope();
    editor->addDecoration(rope.lineStart(2), rope.lineEnd(2), {{"kind", int(CodeEditor::Background)}, {"color", kRed}});
    editor->addDecoration(0, 3, {{"kind", int(CodeEditor::Underline)}, {"color", kRed}});
    QTRY_VERIFY(hasColor(view->grabWindow(), rowRect(editor, 2), kRed));
    QVERIFY(editor->fold(0));
    QTRY_VERIFY(!hasColor(view->grabWindow(), rowRect(editor, 2), kRed));
    const QImage image = view->grabWindow();
    QVERIFY(hasColor(image, rowRect(editor, 0), kRed)); // the header's own underline stays
    for (int row = 1; row < 4; ++row)
      QVERIFY(!hasColor(image, rowRect(editor, row), kRed));
    QVERIFY(editor->unfold(0));
    QTRY_VERIFY(hasColor(view->grabWindow(), rowRect(editor, 2), kRed));
  }

  void rangeAcrossAFoldStartsAfterIt() {
    auto [view, editor] = showEditor();
    editor->setText(kCode);
    const auto &rope = editor->document()->rope();
    // From inside the fold (line 2) to the end of line 5 ("}"), which stays visible.
    editor->addDecoration(rope.lineStart(2), rope.lineEnd(5), {{"kind", int(CodeEditor::Background)}, {"color", kRed}});
    editor->fold(0);
    QTRY_VERIFY(hasColor(view->grabWindow(), rowRect(editor, 1), kRed)); // "}" is row 1 now
    QVERIFY(!hasColor(view->grabWindow(), rowRect(editor, 0), kRed));
  }

  void endOfLineTextWidensTheContentAndIsDrawn() {
    auto [view, editor] = showEditor();
    editor->setText(QStringLiteral("ab\ncd"));
    QTRY_VERIFY(editor->contentWidth() > 0);
    const qreal before = editor->contentWidth();
    editor->addDecoration(0, 2, {{"kind", int(CodeEditor::EndOfLineText)}, {"text", "ERROR HERE"}, {"severity", 1}});
    const qreal cell = editor->metrics().cellAdvance();
    QTRY_VERIFY(editor->contentWidth() >= before + 10 * cell);
    const QColor bg = editor->theme()->background(), band = editor->theme()->currentLine();
    const QRect beyondText(int(editor->gutterWidth() + 5 * cell), 0, int(8 * cell), int(editor->metrics().lineHeight()));
    QTRY_VERIFY(hasInk(view->grabWindow(), beyondText, bg, band));
    // The second line has none.
    QVERIFY(!hasInk(view->grabWindow(), QRect(beyondText.x(), beyondText.height(), beyondText.width(), beyondText.height()), bg, band));
    // Clicking in the virtual text puts the cursor at the end of the line, never past it.
    QCOMPARE(editor->positionAt(editor->gutterWidth() + 9 * cell, 2), 2);
    editor->clearDecorations();
    QTRY_VERIFY(!hasInk(view->grabWindow(), beyondText, bg, band)); // (the content width only ever grows)
  }

  void endOfLineTextIsCutToTheWrapWidth() {
    auto [view, editor] = showEditor(300, 200);
    editor->setText(QStringLiteral("ab"));
    editor->setWrapMode(CodeEditor::WrapAtViewport);
    editor->addDecoration(0, 2, {{"kind", int(CodeEditor::EndOfLineText)}, {"text", QString(200, u'm')}});
    QTRY_VERIFY(editor->displayMap().rowCount() == 1);
    // The row still fits the item: the message is cut short instead of running past the edge.
    QTRY_VERIFY(editor->contentWidth() <= editor->textViewportWidth() + 1);
  }

  void gutterIconColumnShowsIconsOnTheirLines() {
    auto [view, editor] = showEditor();
    editor->setText(QStringLiteral("a\nb\nc"));
    auto *column = new qce::DecorationColumn(editor);
    editor->addGutterColumn(column);
    QTRY_VERIFY(editor->gutterWidth() > 0);
    const int line = 1;
    const auto &rope = editor->document()->rope();
    editor->addDecoration(rope.lineStart(line), rope.lineEnd(line), {{"kind", int(CodeEditor::GutterIcon)}, {"severity", 1}});
    const QColor bg = editor->theme()->gutterBackground(), band = editor->theme()->currentLine();
    const int lh = int(editor->metrics().lineHeight());
    auto gutterInk = [&](int row) {
      return hasInk(view->grabWindow(), QRect(0, row * lh, int(editor->gutterWidth()), lh), bg, band);
    };
    QTRY_VERIFY(gutterInk(1));
    QVERIFY(!gutterInk(0));
    QVERIFY(!gutterInk(2));
    editor->document()->insert(0, QStringLiteral("new\n"));
    QTRY_VERIFY(gutterInk(2));
    QVERIFY(!gutterInk(1));
    editor->clearDecorations();
    QTRY_VERIFY(!gutterInk(2));
  }
};

QTEST_MAIN(TstDecorations)
#include "tst_decorations.moc"
