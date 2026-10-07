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
const QColor kBlue(0x00, 0x00, 0xff);
const QColor kGreen(0x00, 0xff, 0x00);

bool hasColor(const QImage &image, const QRect &rect, const QColor &color) {
  for (int y = rect.top(); y < rect.bottom() && y < image.height(); ++y)
    for (int x = rect.left(); x < rect.right() && x < image.width(); ++x)
      if (image.pixelColor(x, y) == color)
        return true;
  return false;
}

// A pixel that is mostly red: an antialiased red line over the dark theme.
bool hasReddish(const QImage &image, const QRect &rect) {
  for (int y = rect.top(); y < rect.bottom() && y < image.height(); ++y)
    for (int x = rect.left(); x < rect.right() && x < image.width(); ++x) {
      const QColor c = image.pixelColor(x, y);
      if (c.red() > 120 && c.green() < 90 && c.blue() < 90)
        return true;
    }
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

  void squiggleIsDrawnUnderItsRangeOnly() {
    auto [view, editor] = showEditor();
    editor->setText(QStringLiteral("alpha\nbeta gamma"));
    editor->addDecoration(6, 10, {{"kind", int(CodeEditor::Squiggle)}, {"color", kRed}});
    const int lh = int(editor->metrics().lineHeight());
    const qreal cell = editor->metrics().cellAdvance();
    const QRect below(int(editor->gutterWidth()), lh + lh * 2 / 3, 400, lh - lh * 2 / 3);
    QTRY_VERIFY(hasReddish(view->grabWindow(), below));
    const QImage image = view->grabWindow();
    QVERIFY(!hasReddish(image, rowRect(editor, 0)));
    // "beta" is four cells wide: nothing under " gamma".
    const QRect afterRange(int(editor->gutterWidth() + 5 * cell), lh, 200, lh);
    QVERIFY(!hasReddish(image, afterRange));
    // The wave sits at the bottom of the row, not through the text.
    QVERIFY(!hasReddish(image, QRect(int(editor->gutterWidth()), lh, int(4 * cell), lh / 3)));
  }

  void squigglesOnWrappedRowsAndOnEmptyRanges() {
    auto [view, editor] = showEditor(300, 200);
    editor->setText(QString(60, u'x') + u'\n' + QStringLiteral("tail"));
    editor->setWrapMode(CodeEditor::WrapAtViewport);
    editor->addDecoration(5, 55, {{"kind", int(CodeEditor::Squiggle)}, {"color", kRed}});
    QTRY_VERIFY(editor->displayMap().rowCountOfLine(0) >= 3);
    const int rows = int(editor->displayMap().rowCountOfLine(0));
    const int lh = int(editor->metrics().lineHeight());
    auto below = [&](int row) { return QRect(int(editor->gutterWidth()), row * lh + lh * 2 / 3, 200, lh - lh * 2 / 3); };
    QTRY_VERIFY(hasReddish(view->grabWindow(), below(0)));
    const QImage image = view->grabWindow();
    for (int row = 0; row < rows; ++row)
      QVERIFY2(hasReddish(image, below(row)), qPrintable(QStringLiteral("row %1").arg(row)));
    QVERIFY(!hasReddish(image, below(rows))); // "tail"
    // A diagnostic with an empty range still shows one cell of wave.
    editor->clearDecorations();
    editor->addDecoration(rows > 0 ? 62 : 0, 62, {{"kind", int(CodeEditor::Squiggle)}, {"color", kRed}});
    QTRY_VERIFY(hasReddish(view->grabWindow(), below(rows)));
  }

  void squigglePiecesAreReusedAndTexturesAreSharedPerColor() {
    auto [view, editor] = showEditor();
    editor->setText(QStringLiteral("alpha\nbeta\ngamma\ndelta"));
    for (int line = 0; line < 4; ++line) {
      const auto &rope = editor->document()->rope();
      editor->addDecoration(rope.lineStart(line), rope.lineEnd(line), {{"kind", int(CodeEditor::Squiggle)}, {"color", kRed}});
    }
    QTRY_COMPARE(editor->renderStats().scene.squigglePieces, 4);
    const auto first = editor->renderStats().scene;
    QCOMPARE(first.squiggleTexturesCreated, 1); // one color, one texture
    QCOMPARE(first.squiggleNodesCreated, 4);
    // Frames that change nothing about the squiggles create nothing.
    editor->setCursorPosition(7);
    editor->setCursorPosition(12);
    QTest::qWait(50);
    QCOMPARE(editor->renderStats().scene.squiggleNodesCreated, 4);
    QCOMPARE(editor->renderStats().scene.squiggleTexturesCreated, 1);
    // A second color adds one texture.
    editor->addDecoration(0, 2, {{"kind", int(CodeEditor::Squiggle)}, {"color", QColor(0, 0xff, 0)}});
    QTRY_COMPARE(editor->renderStats().scene.squigglePieces, 5);
    QCOMPARE(editor->renderStats().scene.squiggleTexturesCreated, 2);
    // Removing them frees the nodes.
    editor->clearDecorations();
    QTRY_COMPARE(editor->renderStats().scene.squigglePieces, 0);
  }

  void diagnosticsFromLspJsonShowAsSquigglesIconsAndMessages() {
    auto [view, editor] = showEditor();
    editor->setText(QStringLiteral("let a = 1;\nlet b = oops;\n"));
    auto *column = new qce::DecorationColumn(editor);
    editor->addGutterColumn(column);
    QTRY_VERIFY(editor->gutterWidth() > 0);
    QSignalSpy countSpy(editor, &CodeEditor::diagnosticsChanged);
    const QVariantMap error{
      {"range", QVariantMap{{"start", QVariantMap{{"line", 1}, {"character", 8}}}, {"end", QVariantMap{{"line", 1}, {"character", 12}}}}},
      {"severity", 1}, {"message", "oops is not defined"}, {"code", "E1"}, {"source", "lint"}};
    editor->setDiagnostics(QVariantList{error});
    QCOMPARE(editor->diagnosticCount(), 1);
    QVERIFY(countSpy.count() >= 1);
    const int lh = int(editor->metrics().lineHeight());
    const QColor errorColor = editor->theme()->diagnosticError();
    auto reddish = [&](const QRect &rect) {
      const QImage image = view->grabWindow();
      for (int y = rect.top(); y < rect.bottom() && y < image.height(); ++y)
        for (int x = rect.left(); x < rect.right() && x < image.width(); ++x) {
          const QColor c = image.pixelColor(x, y);
          if (c.red() > errorColor.red() * 6 / 10 && c.green() < 90 && c.blue() < 90)
            return true;
        }
      return false;
    };
    const QRect textRow1(int(editor->gutterWidth()), lh + lh * 2 / 3, 400, lh - lh * 2 / 3);
    QTRY_VERIFY(reddish(textRow1));              // the squiggle
    QVERIFY(reddish(QRect(0, lh, int(editor->gutterWidth()), lh))); // the icon
    QVERIFY(!reddish(QRect(0, 0, int(editor->gutterWidth()), lh)));
    // The diagnostic comes back with its range as it is now.
    const qsizetype inside = editor->document()->rope().lineStart(1) + 9;
    const QVariantList hit = editor->diagnosticsAt(inside);
    QCOMPARE(hit.size(), 1);
    QCOMPARE(hit[0].toMap().value("message").toString(), QStringLiteral("oops is not defined"));
    QCOMPARE(hit[0].toMap().value("code").toString(), QStringLiteral("E1"));
    editor->document()->insert(0, QStringLiteral("// c\n"));
    const QVariantMap range = editor->diagnosticsAt(inside + 5).first().toMap().value("range").toMap();
    QCOMPARE(range.value("start").toMap().value("line").toInt(), 2);
    QCOMPARE(range.value("start").toMap().value("character").toInt(), 8);
    // End-of-line messages are off until asked for.
    QCOMPARE(editor->diagnosticMessages(), CodeEditor::NoMessages);
    const qreal before = editor->contentWidth();
    editor->setDiagnosticMessages(CodeEditor::EndOfLineMessages);
    QTRY_VERIFY(editor->contentWidth() > before);
    editor->setDiagnosticMessages(CodeEditor::NoMessages);
    editor->clearDiagnostics();
    QCOMPARE(editor->diagnosticCount(), 0);
    QTRY_VERIFY(!reddish(textRow1.translated(0, lh)));
    QVERIFY(editor->diagnosticsAt(inside).isEmpty());
  }

  void gotoNextAndPreviousDiagnosticWrapAndScrollAndUnfold() {
    auto [view, editor] = showEditor(120, 400);
    QString text;
    for (int i = 0; i < 100; ++i)
      text += (i == 20 ? QStringLiteral("block {\n") : i == 21 || i == 22 ? QStringLiteral("  inner %1\n").arg(i) : QStringLiteral("line %1\n").arg(i));
    editor->setText(text);
    auto lsp = [](int line, int severity) {
      return QVariantMap{{"range", QVariantMap{{"start", QVariantMap{{"line", line}, {"character", 1}}},
                                                {"end", QVariantMap{{"line", line}, {"character", 3}}}}},
                         {"severity", severity}, {"message", "m"}};
    };
    QVERIFY(!editor->gotoNextDiagnostic()); // none yet
    editor->setDiagnostics(QVariantList{lsp(5, 1), lsp(21, 2), lsp(70, 3), lsp(90, 4)});
    editor->setCursorPosition(0);
    QVERIFY(editor->gotoNextDiagnostic());
    QCOMPARE(editor->cursorLine(), 5);
    QCOMPARE(editor->cursorColumn(), 1);
    // Down in the text, the view follows.
    editor->fold(20);
    QVERIFY(editor->isFolded(20));
    QVERIFY(editor->gotoNextDiagnostic()); // line 21 is folded away: the fold opens
    QCOMPARE(editor->cursorLine(), 21);
    QVERIFY(!editor->isFolded(20));
    QVERIFY(editor->gotoNextDiagnostic());
    QCOMPARE(editor->cursorLine(), 70);
    const qreal lh = editor->metrics().lineHeight();
    QVERIFY(editor->contentY() <= 70 * lh && editor->contentY() + editor->height() >= 71 * lh);
    QVERIFY(editor->gotoNextDiagnostic());
    QCOMPARE(editor->cursorLine(), 90);
    QVERIFY(editor->gotoNextDiagnostic()); // wraps
    QCOMPARE(editor->cursorLine(), 5);
    QVERIFY(editor->gotoPreviousDiagnostic()); // wraps backwards
    QCOMPARE(editor->cursorLine(), 90);
    QVERIFY(editor->gotoPreviousDiagnostic());
    QCOMPARE(editor->cursorLine(), 70);
    // Only errors and warnings.
    QVERIFY(editor->gotoNextDiagnostic(2));
    QCOMPARE(editor->cursorLine(), 5);
    QVERIFY(!editor->gotoNextDiagnostic(0));
    // The keys.
    editor->setCursorPosition(0);
    QTest::keyClick(view.get(), Qt::Key_F8);
    QCOMPARE(editor->cursorLine(), 5);
    QTest::keyClick(view.get(), Qt::Key_F8);
    QCOMPARE(editor->cursorLine(), 21);
    QTest::keyClick(view.get(), Qt::Key_F8, Qt::ShiftModifier);
    QCOMPARE(editor->cursorLine(), 5);
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

  QRect cellRect(CodeEditor *editor, int row, int column) {
    const int cell = int(editor->metrics().cellAdvance());
    return QRect(int(editor->gutterWidth()) + column * cell + 1, row * int(editor->metrics().lineHeight()), cell - 2,
                 int(editor->metrics().lineHeight()));
  }

  void bracketNextToTheCursorAndItsPartnerAreHighlighted() {
    auto [view, editor] = showEditor();
    editor->theme()->setProperty("bracketMatch", kRed);
    editor->setText(QStringLiteral("(ab)\nxx"));
    QVERIFY(editor->matchBrackets());
    editor->setCursorPosition(0);
    QTRY_VERIFY(hasColor(view->grabWindow(), cellRect(editor, 0, 0), kRed));
    QImage image = view->grabWindow();
    QVERIFY(hasColor(image, cellRect(editor, 0, 3), kRed));
    QVERIFY(!hasColor(image, cellRect(editor, 0, 1), kRed));
    QVERIFY(!hasColor(image, cellRect(editor, 0, 2), kRed));
    // The bracket before the cursor counts when there is none after it.
    editor->setCursorPosition(4);
    QTRY_VERIFY(hasColor(view->grabWindow(), cellRect(editor, 0, 0), kRed));
    QVERIFY(hasColor(view->grabWindow(), cellRect(editor, 0, 3), kRed));
    // Away from any bracket nothing is drawn.
    editor->setCursorPosition(2);
    QTRY_VERIFY(!hasColor(view->grabWindow(), cellRect(editor, 0, 0), kRed));
    QVERIFY(!hasColor(view->grabWindow(), cellRect(editor, 0, 3), kRed));
  }

  void matchBracketsCanBeTurnedOff() {
    auto [view, editor] = showEditor();
    editor->theme()->setProperty("bracketMatch", kRed);
    editor->setText(QStringLiteral("(ab)"));
    editor->setCursorPosition(0);
    QTRY_VERIFY(hasColor(view->grabWindow(), cellRect(editor, 0, 3), kRed));
    QSignalSpy spy(editor, &CodeEditor::matchBracketsChanged);
    editor->setMatchBrackets(false);
    QCOMPARE(spy.count(), 1);
    editor->setMatchBrackets(false);
    QCOMPARE(spy.count(), 1);
    QTRY_VERIFY(!hasColor(view->grabWindow(), cellRect(editor, 0, 3), kRed));
    QVERIFY(!hasColor(view->grabWindow(), cellRect(editor, 0, 0), kRed));
    editor->setMatchBrackets(true);
    QTRY_VERIFY(hasColor(view->grabWindow(), cellRect(editor, 0, 3), kRed));
  }

  void everyCursorGetsItsBracketsHighlighted() {
    auto [view, editor] = showEditor();
    editor->theme()->setProperty("bracketMatch", kRed);
    editor->setText(QStringLiteral("(a)\n[b]"));
    editor->setCursorPosition(0);
    QVERIFY(editor->addCursorBelow());
    QCOMPARE(editor->selectionCount(), 2);
    QTRY_VERIFY(hasColor(view->grabWindow(), cellRect(editor, 1, 2), kRed));
    const QImage image = view->grabWindow();
    for (int row = 0; row < 2; ++row) {
      QVERIFY(hasColor(image, cellRect(editor, row, 0), kRed));
      QVERIFY(hasColor(image, cellRect(editor, row, 2), kRed));
      QVERIFY(!hasColor(image, cellRect(editor, row, 1), kRed));
    }
  }

  void editsUpdateTheHighlight() {
    auto [view, editor] = showEditor();
    editor->theme()->setProperty("bracketMatch", kRed);
    editor->setText(QStringLiteral("(a)"));
    editor->setCursorPosition(0);
    QTRY_VERIFY(hasColor(view->grabWindow(), cellRect(editor, 0, 2), kRed));
    editor->setCursorPosition(1);
    editor->insert(QStringLiteral("bc"));
    editor->setCursorPosition(0);
    QTRY_VERIFY(hasColor(view->grabWindow(), cellRect(editor, 0, 4), kRed));
    QVERIFY(!hasColor(view->grabWindow(), cellRect(editor, 0, 2), kRed));
  }

  void partnerHiddenByAFoldIsSkipped() {
    auto [view, editor] = showEditor();
    editor->theme()->setProperty("bracketMatch", kRed);
    editor->setText(kCode);
    editor->setCursorPosition(13); // after the "{" of "  c {" (line 2), whose "}" is on line 4
    QTRY_VERIFY(hasColor(view->grabWindow(), cellRect(editor, 2, 4), kRed));
    QVERIFY(hasColor(view->grabWindow(), cellRect(editor, 4, 2), kRed));
    QVERIFY(editor->fold(2));
    QTRY_VERIFY(!hasColor(view->grabWindow(), cellRect(editor, 4, 2), kRed));
    const QImage image = view->grabWindow();
    QVERIFY(hasColor(image, cellRect(editor, 2, 4), kRed));
    QVERIFY(!hasColor(image, cellRect(editor, 3, 0), kRed)); // the "}" of line 5 is not its partner
  }

  // A strip a few pixels wide around the guide at `column` of a row.
  QRect guideRect(CodeEditor *editor, int row, int column) {
    const int x = int(editor->gutterWidth()) + qRound(column * editor->metrics().cellAdvance());
    return QRect(column ? x - 1 : x, row * int(editor->metrics().lineHeight()), 3, int(editor->metrics().lineHeight()));
  }
  int setUpGuides(CodeEditor *editor, const QString &text) {
    editor->theme()->setProperty("indentGuide", kBlue);
    editor->theme()->setProperty("indentGuideActive", kGreen);
    editor->setDetectIndentation(false);
    editor->setIndentWidth(4);
    editor->setText(text);
    return int(text.size());
  }

  void guidesAreDrawnAtEveryIndentStep() {
    auto [view, editor] = showEditor();
    const int end = setUpGuides(editor, QStringLiteral("a\n        b\n    c\nd"));
    editor->setCursorPosition(end); // top level: no active block
    QVERIFY(editor->showIndentGuides());
    QTRY_VERIFY(hasColor(view->grabWindow(), guideRect(editor, 1, 4), kBlue));
    const QImage image = view->grabWindow();
    QVERIFY(hasColor(image, guideRect(editor, 1, 0), kBlue));
    QVERIFY(!hasColor(image, guideRect(editor, 1, 8), kBlue));
    QVERIFY(hasColor(image, guideRect(editor, 2, 0), kBlue));
    QVERIFY(!hasColor(image, guideRect(editor, 2, 4), kBlue));
    QVERIFY(!hasColor(image, guideRect(editor, 0, 0), kBlue));
    QVERIFY(!hasColor(image, guideRect(editor, 3, 0), kBlue));
  }

  void guidesShowOverTheSelection() {
    auto [view, editor] = showEditor();
    editor->theme()->setProperty("selection", kRed);
    const int end = setUpGuides(editor, QStringLiteral("a\n        b\n    c\nd"));
    editor->setCursorPosition(end);
    editor->selectAll();
    QTRY_VERIFY(hasColor(view->grabWindow(), guideRect(editor, 1, 4), kBlue));
    const QImage image = view->grabWindow();
    QVERIFY(hasColor(image, guideRect(editor, 1, 0), kBlue));
    QVERIFY(hasColor(image, guideRect(editor, 1, 4), kRed)); // the selection is there around it
  }

  void blankLinesContinueTheGuides() {
    auto [view, editor] = showEditor();
    const int end = setUpGuides(editor, QStringLiteral("a\n        b\n\n        c\nd"));
    editor->setCursorPosition(end);
    QTRY_VERIFY(hasColor(view->grabWindow(), guideRect(editor, 2, 4), kBlue));
    QVERIFY(hasColor(view->grabWindow(), guideRect(editor, 2, 0), kBlue));
  }

  void guidesCanBeTurnedOff() {
    auto [view, editor] = showEditor();
    const int end = setUpGuides(editor, QStringLiteral("a\n    b\nc"));
    editor->setCursorPosition(end);
    QTRY_VERIFY(hasColor(view->grabWindow(), guideRect(editor, 1, 0), kBlue));
    QSignalSpy spy(editor, &CodeEditor::showIndentGuidesChanged);
    editor->setShowIndentGuides(false);
    editor->setShowIndentGuides(false);
    QCOMPARE(spy.count(), 1);
    QTRY_VERIFY(!hasColor(view->grabWindow(), guideRect(editor, 1, 0), kBlue));
    editor->setShowIndentGuides(true);
    QTRY_VERIFY(hasColor(view->grabWindow(), guideRect(editor, 1, 0), kBlue));
  }

  void activeGuideFollowsTheBracketBlock() {
    auto [view, editor] = showEditor();
    const int end = setUpGuides(editor, QStringLiteral("f {\n    a\n    b\n    c\n}\ntail"));
    editor->setCursorPosition(3); // right after the "{"
    QTRY_VERIFY(hasColor(view->grabWindow(), guideRect(editor, 2, 0), kGreen));
    QImage image = view->grabWindow();
    for (int row = 1; row <= 3; ++row) {
      QVERIFY(hasColor(image, guideRect(editor, row, 0), kGreen));
      QVERIFY(!hasColor(image, guideRect(editor, row, 0), kBlue));
    }
    QVERIFY(!hasColor(image, guideRect(editor, 0, 0), kGreen));
    QVERIFY(!hasColor(image, guideRect(editor, 4, 0), kGreen));
    // Inside the block, away from any bracket, the enclosing pair is used.
    editor->setCursorPosition(10);
    QTRY_VERIFY(hasColor(view->grabWindow(), guideRect(editor, 1, 0), kGreen));
    QVERIFY(hasColor(view->grabWindow(), guideRect(editor, 3, 0), kGreen));
    // Outside every block the guides are plain.
    editor->setCursorPosition(end);
    QTRY_VERIFY(!hasColor(view->grabWindow(), guideRect(editor, 2, 0), kGreen));
    QVERIFY(hasColor(view->grabWindow(), guideRect(editor, 2, 0), kBlue));
  }

  void onlyTheInnermostBlockIsActive() {
    auto [view, editor] = showEditor();
    setUpGuides(editor, QStringLiteral("f {\n    g {\n        a\n        b\n    }\n}\ntail"));
    editor->setCursorPosition(21); // after the "a" of line 2
    QTRY_VERIFY(hasColor(view->grabWindow(), guideRect(editor, 2, 4), kGreen));
    const QImage image = view->grabWindow();
    QVERIFY(hasColor(image, guideRect(editor, 2, 0), kBlue));
    QVERIFY(!hasColor(image, guideRect(editor, 2, 0), kGreen));
    QVERIFY(hasColor(image, guideRect(editor, 3, 4), kGreen));
    QVERIFY(hasColor(image, guideRect(editor, 1, 0), kBlue)); // "g {" is inside the outer block only
    QVERIFY(!hasColor(image, guideRect(editor, 1, 0), kGreen));
  }

  void guidesStayInsideTheHangingIndent() {
    auto [view, editor] = showEditor();
    const int end = setUpGuides(editor, QStringLiteral("        ") + QString(200, u'x') + QStringLiteral("\nend"));
    editor->setCursorPosition(end);
    editor->setWrapMode(CodeEditor::WrapAtViewport);
    QTRY_VERIFY(hasColor(view->grabWindow(), guideRect(editor, 1, 4), kBlue));
    QVERIFY(hasColor(view->grabWindow(), guideRect(editor, 1, 0), kBlue));
    editor->setWrapIndent(false);
    QTRY_VERIFY(!hasColor(view->grabWindow(), guideRect(editor, 1, 4), kBlue));
    QVERIFY(!hasColor(view->grabWindow(), guideRect(editor, 1, 0), kBlue));
    QVERIFY(hasColor(view->grabWindow(), guideRect(editor, 0, 0), kBlue));
  }

  void foldedLinesTakeTheirGuidesWithThem() {
    auto [view, editor] = showEditor();
    const int end = setUpGuides(editor, QStringLiteral("a {\n    x\n    c {\n        z\n    }\n}\ntail"));
    editor->setCursorPosition(end);
    QTRY_VERIFY(hasColor(view->grabWindow(), guideRect(editor, 3, 4), kBlue));
    QVERIFY(editor->fold(2));
    QTRY_VERIFY(!hasColor(view->grabWindow(), guideRect(editor, 3, 4), kBlue));
    // Row 3 is now the "    }" of the folded block: one level deep, not two.
    QVERIFY(hasColor(view->grabWindow(), guideRect(editor, 3, 0), kBlue));
    QVERIFY(hasColor(view->grabWindow(), guideRect(editor, 1, 0), kBlue));
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
