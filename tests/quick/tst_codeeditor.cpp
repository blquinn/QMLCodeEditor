#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>
#include <QtQml/qqmlextensionplugin.h>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickView>
#include <QtQuick/QQuickWindow>
#include <QtQuick/qsgrendererinterface.h>
#include <QtTest>

#include "quick/codeeditor.h"

Q_IMPORT_QML_PLUGIN(me_blq_qmlcodeeditorPlugin)

class TstCodeEditor : public QObject {
  Q_OBJECT
  // One engine for every view: each QQmlEngine starts a loader thread, and creating that many threads
  // from uninstrumented Qt trips a ThreadSanitizer thread-registry check under the tsan preset.
  QQmlEngine m_engine;

private slots:
  void initTestCase() { QQuickWindow::setGraphicsApi(QSGRendererInterface::Software); }

  void rendersBackground() {
    QQuickView view(&m_engine, nullptr);
    view.setSource(QUrl::fromLocalFile(QFINDTESTDATA("editor.qml")));
    QCOMPARE(view.status(), QQuickView::Ready);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    auto *editor = qobject_cast<CodeEditor *>(view.rootObject());
    QVERIFY(editor);
    QVERIFY(editor->flags().testFlag(QQuickItem::ItemHasContents));
    const QImage image = view.grabWindow();
    QVERIFY(!image.isNull());
    QCOMPARE(image.pixelColor(10, 90), QColor(0x1e, 0x1e, 0x1e));
  }

  void themeSwitchRepaints() {
    QQuickView view(&m_engine, nullptr);
    view.setSource(QUrl::fromLocalFile(QFINDTESTDATA("editor.qml")));
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    auto *editor = qobject_cast<CodeEditor *>(view.rootObject());
    QVERIFY(editor && editor->theme());
    qce::Theme light;
    light.assign(*std::unique_ptr<qce::Theme>(qce::Theme::createLight()));
    editor->setTheme(&light);
    QCOMPARE(editor->theme(), &light);
    QTRY_COMPARE(view.grabWindow().pixelColor(10, 90), QColor(0xff, 0xff, 0xff));
    light.setProperty("background", QColor(0x10, 0x20, 0x30));
    QTRY_COMPARE(view.grabWindow().pixelColor(10, 90), QColor(0x10, 0x20, 0x30));
    editor->setTheme(nullptr); // back to the owned default
    QTRY_COMPARE(view.grabWindow().pixelColor(10, 90), QColor(0x1e, 0x1e, 0x1e));
  }

  void themeMapsSpansToFormats() {
    std::unique_ptr<qce::Theme> theme(qce::Theme::createDark());
    using qce::HighlightSpan;
    using qce::TokenStyle;
    const auto ranges = theme->formatRanges(
      {{0, 3, TokenStyle::Keyword}, {4, 2, TokenStyle::Default}, {7, 5, TokenStyle::Comment}}
    );
    QCOMPARE(ranges.size(), 2);
    QCOMPARE(ranges[0].start, 0);
    QCOMPARE(ranges[0].length, 3);
    QCOMPARE(ranges[0].format.foreground().color(), QColor(QStringLiteral("#569cd6")));
    QVERIFY(ranges[1].format.fontItalic());
    QCOMPARE(theme->charFormat(TokenStyle::Default).foreground().color(), theme->foreground());
    // Changing the foreground reaches styles the theme doesn't list.
    theme->setProperty("foreground", QColor(Qt::red));
    QCOMPARE(theme->charFormat(TokenStyle::Default).foreground().color(), QColor(Qt::red));
  }

  void highlighterInvalidationRepaints() {
    struct Fake : qce::Highlighter {
      QList<QList<qce::HighlightSpan>>
      highlightLines(const qce::TextSnapshot &, qsizetype, qsizetype) override {
        return {};
      }
    } fake;
    CodeEditor editor;
    QVERIFY(editor.highlighter());
    editor.setHighlighter(&fake);
    QCOMPARE(editor.highlighter(), &fake);
    emit fake.invalidated(0, 3); // must not crash with an item that has no window
    editor.setHighlighter(nullptr);
    QVERIFY(editor.highlighter() != &fake);
  }

  void layoutsOnlyForViewportOfHugeDocument() {
    QQuickView view(&m_engine, nullptr);
    view.setSource(QUrl::fromLocalFile(QFINDTESTDATA("editor.qml")));
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    auto *editor = qobject_cast<CodeEditor *>(view.rootObject());
    QString text;
    text.reserve(10'000'000);
    for (int i = 0; i < 1'000'000; ++i)
      text += QStringLiteral("line number %1\n").arg(i);
    editor->setText(text);
    QCOMPARE(editor->lineCount(), 1'000'001);
    QTRY_VERIFY(editor->renderStats().rowsInPlan > 5); // past the startup frame
    const auto stats = editor->renderStats();
    const qsizetype visible = qsizetype(std::ceil(editor->height() / editor->metrics().lineHeight())) + 1;
    QVERIFY2(stats.rowsInPlan <= visible * 2 + 12, qPrintable(QString::number(stats.rowsInPlan)));
    QCOMPARE(qsizetype(stats.layoutsCreated), stats.rowsInPlan + 1); // plus the empty startup line

    // Jump to the middle: a different window, same bounded work.
    editor->setContentY(500'000 * editor->metrics().lineHeight());
    QTRY_VERIFY(editor->renderStats().layoutsCreated > stats.layoutsCreated);
    QVERIFY(editor->renderStats().rowsInPlan <= visible * 2 + 12);
    QVERIFY(editor->renderStats().layoutsCached <= 256);
  }

  void editInvalidatesOnlyAffectedLayouts() {
    QQuickView view(&m_engine, nullptr);
    view.setSource(QUrl::fromLocalFile(QFINDTESTDATA("editor.qml")));
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    auto *editor = qobject_cast<CodeEditor *>(view.rootObject());
    QTRY_VERIFY(editor->renderStats().layoutsCreated > 0); // the empty document's one line
    const quint64 base = editor->renderStats().layoutsCreated;
    editor->setText(QStringLiteral("a\nb\nc\nd\ne"));
    QTRY_COMPARE(editor->renderStats().layoutsCreated, base + 5);
    editor->document()->insert(editor->document()->rope().lineStart(2), u"x");
    QTRY_COMPARE(editor->renderStats().layoutsCreated, base + 6); // only line 2 was laid out again
    editor->document()->insert(0, u"first\n"); // lines shift; line 0 is new, the rest renumbered
    QTRY_COMPARE(editor->renderStats().layoutsCreated, base + 8);
  }

  void drawsTextAndPoolsNodes() {
    QQuickView view(&m_engine, nullptr);
    view.setSource(QUrl::fromLocalFile(QFINDTESTDATA("editor.qml")));
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    auto *editor = qobject_cast<CodeEditor *>(view.rootObject());
    QString text;
    for (int i = 0; i < 5000; ++i)
      text += QStringLiteral("MMMMMMMMMMMMMMMMMMMM\n");
    editor->setText(text);
    QTRY_VERIFY(editor->renderStats().rowsInPlan > 5);

    auto hasInk = [](const QImage &image) {
      const QColor bg(0x1e, 0x1e, 0x1e);
      for (int y = 0; y < 14; ++y)
        for (int x = 0; x < 100; ++x)
          if (image.pixelColor(x, y) != bg)
            return true;
      return false;
    };
    QTRY_VERIFY(hasInk(view.grabWindow()));
    const auto first = editor->renderStats();
    QVERIFY(first.scene.nodesCreated > 0);
    QVERIFY(first.scene.linesFilled >= first.scene.nodesCreated);

    // Settle mid-document, where the margin exists on both sides.
    const qreal lh = editor->metrics().lineHeight();
    editor->setContentY(1000 * lh);
    view.grabWindow();
    QTRY_VERIFY(view.grabWindow().pixelColor(0, 0).isValid());
    auto settled = editor->renderStats();
    // Scrolling by a row at a time swaps one row out for one in: same layouts count growth of one
    // per row, and every node comes from the pool.
    for (int step = 1; step <= 3; ++step) {
      editor->setContentY((1000 + step) * lh);
      view.grabWindow();
    }
    auto after = editor->renderStats();
    QCOMPARE(after.scene.nodesCreated, settled.scene.nodesCreated);
    QCOMPARE(after.layoutsCreated, settled.layoutsCreated + 3);
    QVERIFY(after.scene.nodesRecycled >= settled.scene.nodesRecycled + 3);
    // Scrolling back over rows that are still cached lays out nothing.
    for (int step = 2; step >= 0; --step) {
      editor->setContentY((1000 + step) * lh);
      view.grabWindow();
    }
    QCOMPARE(editor->renderStats().layoutsCreated, after.layoutsCreated);
    QCOMPARE(editor->renderStats().scene.nodesCreated, after.scene.nodesCreated);

    // Pixel-sized scrolling inside the window touches no node at all except the scroll matrix.
    editor->setContentY(1003 * lh);
    view.grabWindow();
    const auto beforePixels = editor->renderStats();
    for (int px = 1; px <= 5; ++px) {
      editor->setContentY(1003 * lh + px);
      view.grabWindow();
    }
    const auto afterPixels = editor->renderStats();
    QCOMPARE(afterPixels.layoutsCreated, beforePixels.layoutsCreated);
    QCOMPARE(afterPixels.scene.linesFilled, beforePixels.scene.linesFilled);
    QCOMPARE(afterPixels.scene.matrixUpdates, beforePixels.scene.matrixUpdates);
    QCOMPARE(afterPixels.scene.nodesRecycled, beforePixels.scene.nodesRecycled);

    // A long scroll in small steps reuses the same nodes over and over.
    const quint64 createdBefore = editor->renderStats().scene.nodesCreated;
    for (int step = 1004; step < 1400; ++step) {
      editor->setContentY(step * lh);
      view.grabWindow();
    }
    after = editor->renderStats();
    QVERIFY2(
      after.scene.nodesCreated <= createdBefore + 8, qPrintable(QString::number(after.scene.nodesCreated))
    );
    QVERIFY(after.scene.nodesRecycled > 100);
    QVERIFY(after.layoutsCached <= 256);
    // Ink is still visible after all the recycling.
    QVERIFY(view.grabWindow().pixelColor(0, 0) != QColor());
  }

  void scrollModelClampsAndReportsSize() {
    QQuickView view(&m_engine, nullptr);
    view.setSource(QUrl::fromLocalFile(QFINDTESTDATA("editor.qml")));
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    auto *editor = qobject_cast<CodeEditor *>(view.rootObject());
    const qreal lh = editor->metrics().lineHeight();
    QString text;
    for (int i = 0; i < 1000; ++i)
      text += QStringLiteral(
        "0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789\n"
      );
    editor->setText(text);
    QCOMPARE(editor->contentHeight(), 1001 * lh);
    QSignalSpy heightSpy(editor, &CodeEditor::contentHeightChanged);
    QSignalSpy ySpy(editor, &CodeEditor::contentYChanged);

    editor->setContentY(-50);
    QCOMPARE(editor->contentY(), 0.0);
    editor->setContentY(1e9);
    QCOMPARE(editor->contentY(), editor->contentHeight() - editor->height());
    QCOMPARE(ySpy.count(), 1);
    editor->setContentY(0);

    // Width is learned from the lines that were laid out.
    QTRY_VERIFY(editor->contentWidth() > editor->width());
    editor->setContentX(1e9);
    QCOMPARE(editor->contentX(), editor->contentWidth() - editor->width());
    editor->setContentX(0);

    // Shrinking the document re-clamps the scroll offset.
    editor->setContentY(900 * lh);
    editor->setText(QStringLiteral("a\nb"));
    QCOMPARE(editor->contentY(), 0.0);
    QCOMPARE(editor->contentHeight(), 2 * lh);
    QVERIFY(heightSpy.count() >= 1);
  }

  void wheelScrolls() {
    QQuickView view(&m_engine, nullptr);
    view.setSource(QUrl::fromLocalFile(QFINDTESTDATA("editor.qml")));
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    auto *editor = qobject_cast<CodeEditor *>(view.rootObject());
    QString text;
    for (int i = 0; i < 1000; ++i)
      text += QStringLiteral("line\n");
    editor->setText(text);
    const qreal lh = editor->metrics().lineHeight();
    QWheelEvent down(
      QPointF(10, 10), QPointF(10, 10), QPoint(0, 0), QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
      Qt::NoScrollPhase, false
    );
    QGuiApplication::sendEvent(&view, &down);
    QCOMPARE(editor->contentY(), 3 * lh);
    QWheelEvent pixels(
      QPointF(10, 10), QPointF(10, 10), QPoint(0, -7), QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
      Qt::NoScrollPhase, false
    );
    QGuiApplication::sendEvent(&view, &pixels);
    QCOMPARE(editor->contentY(), 3 * lh + 7); // pixel delta wins over angle delta
  }

  void positionAndRectRoundTrip() {
    QQuickView view(&m_engine, nullptr);
    view.setSource(QUrl::fromLocalFile(QFINDTESTDATA("editor.qml")));
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    auto *editor = qobject_cast<CodeEditor *>(view.rootObject());
    editor->setText(QStringLiteral("alpha\n\tbeta\ngamma \u4e2d\u6587 end\n"));
    const qreal lh = editor->metrics().lineHeight();
    const auto *rope = &editor->document()->rope();
    for (qsizetype offset :
         {qsizetype(0), qsizetype(3), rope->lineStart(1) + 1, rope->lineStart(1) + 3, rope->lineStart(2) + 4,
          rope->lineStart(2) + 8, rope->lineEnd(2)}) {
      const QRectF rect = editor->rectForPosition(offset);
      QVERIFY2(rect.height() == lh, "cell height");
      // The middle of the cell's left edge maps back to the offset.
      QCOMPARE(editor->positionAt(rect.left() + 0.01, rect.center().y()), offset);
    }
    // Below the last line and left of the text clamp to document edges.
    QCOMPARE(editor->positionAt(-20, 100000), rope->length());
    QCOMPARE(editor->positionAt(-20, 0), 0);
    // Scrolling moves rectangles but not offsets.
    const QRectF before = editor->rectForPosition(rope->lineStart(1));
    editor->setText(QString(200, QLatin1Char('x')).repeated(1).append(u'\n').repeated(100));
    editor->setContentY(lh * 3);
    const QRectF row3 = editor->rectForPosition(editor->document()->rope().lineStart(3));
    QCOMPARE(row3.top(), 0.0);
    QVERIFY(before.top() > 0);
  }

  void selectionCursorAndCurrentLineGeometry() {
    QQuickView view(&m_engine, nullptr);
    view.setSource(QUrl::fromLocalFile(QFINDTESTDATA("editor.qml")));
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    auto *editor = qobject_cast<CodeEditor *>(view.rootObject());
    editor->forceActiveFocus();
    editor->setCursorBlinkInterval(0);
    editor->setText(QStringLiteral("MMMMMMMMMM\nMMMMMMMMMM\nMMMMMMMMMM"));
    QTRY_VERIFY(editor->renderStats().rowsInPlan >= 3);
    const qreal lh = editor->metrics().lineHeight();
    const qreal adv = editor->metrics().cellAdvance();
    const auto *theme = editor->theme();
    // Sample along the bottom of a row, below the glyph bodies of 'M'.
    auto pixel = [&](qreal x, int row) {
      return view.grabWindow().pixelColor(int(x), int((row + 1) * lh) - 1);
    };

    // No selection: the cursor line is highlighted, and the cursor is a 2px bar at its column.
    editor->setCursorPosition(4);
    QTRY_COMPARE(pixel(150, 0), theme->currentLine());
    QCOMPARE(pixel(150, 1), theme->background());
    QCOMPARE(pixel(adv * 4 + 1, 0), theme->cursor());
    QCOMPARE(pixel(adv * 2, 0), theme->currentLine());

    // A selection replaces the line highlight and covers exactly its cells.
    editor->select(2, 5);
    QTRY_COMPARE(pixel(adv * 3 + 1, 0), theme->selection());
    QCOMPARE(pixel(adv * 1 + 1, 0), theme->background());
    QCOMPARE(pixel(adv * 6, 0), theme->background());
    QCOMPARE(editor->selectionStart(), 2);
    QCOMPARE(editor->selectionEnd(), 5);
    QCOMPARE(editor->cursorPosition(), 5);

    // Across lines: the first row is selected to its end (line break included), the middle row
    // fully, the last row up to the end column.
    editor->select(8, editor->document()->rope().lineStart(2) + 3);
    QTRY_COMPARE(pixel(adv * 10 + 1, 0), theme->selection()); // the line break cell
    QCOMPARE(pixel(adv * 5, 1), theme->selection());
    QCOMPARE(pixel(adv * 2, 2), theme->selection());
    QCOMPARE(pixel(adv * 4, 2), theme->background());
    QCOMPARE(pixel(adv * 7, 0), theme->background());
  }

  void selectionFollowsEdits() {
    CodeEditor editor;
    editor.setText(QStringLiteral("0123456789"));
    editor.select(2, 5);
    QSignalSpy spy(&editor, &CodeEditor::selectionChanged);
    editor.document()->insert(0, u"ZZ");
    QCOMPARE(editor.selectionStart(), 4);
    QCOMPARE(editor.selectionEnd(), 7);
    QCOMPARE(spy.count(), 1);
    editor.document()->insert(editor.selectionEnd(), u"!"); // typing at the head moves the cursor on
    QCOMPARE(editor.cursorPosition(), 8);
    // Offsets are clamped and never land inside a surrogate pair.
    editor.setText(QStringLiteral("a\U0001F600b"));
    editor.setCursorPosition(2);
    QCOMPARE(editor.cursorPosition(), 1);
    editor.setCursorPosition(999);
    QCOMPARE(editor.cursorPosition(), 4);
  }

  void cursorBlinks() {
    QQuickView view(&m_engine, nullptr);
    view.setSource(QUrl::fromLocalFile(QFINDTESTDATA("editor.qml")));
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    auto *editor = qobject_cast<CodeEditor *>(view.rootObject());
    editor->forceActiveFocus();
    editor->setText(QStringLiteral("MMMMMMMMMM"));
    editor->setCursorPosition(4);
    editor->setCursorBlinkInterval(30);
    const qreal lh = editor->metrics().lineHeight();
    const qreal adv = editor->metrics().cellAdvance();
    QSignalSpy spy(editor, &CodeEditor::cursorVisibleChanged);
    QTRY_VERIFY(spy.count() >= 2);
    // Each phase is a repaint of the same scene: no layouts, no nodes.
    const auto before = editor->renderStats();
    bool sawShown = false, sawHidden = false;
    for (int i = 0; i < 60 && !(sawShown && sawHidden); ++i) {
      const bool visible = editor->cursorVisible();
      const QColor c = view.grabWindow().pixelColor(int(adv * 4 + 1), int(lh) - 1);
      // The phase may flip between reading it and rendering, so only count matching samples.
      if (visible == editor->cursorVisible()) {
        if (visible && c == editor->theme()->cursor())
          sawShown = true;
        if (!visible && c != editor->theme()->cursor())
          sawHidden = true;
      }
      QTest::qWait(10);
    }
    QVERIFY(sawShown);
    QVERIFY(sawHidden);
    QCOMPARE(editor->renderStats().layoutsCreated, before.layoutsCreated);
    QCOMPARE(editor->renderStats().scene.nodesCreated, before.scene.nodesCreated);
    editor->setCursorBlinkInterval(0);
    QVERIFY(editor->cursorVisible());
  }

  // Does any pixel in [x0, x1) of the first row differ from the empty row (background or
  // current-line highlight)?
  static bool inkInColumns(const QImage &image, qreal x0, qreal x1, int rowHeight) {
    const QColor bg(0x1e, 0x1e, 0x1e), currentLine(0x2a, 0x2d, 0x2e); // default dark theme
    for (int y = 0; y < rowHeight; ++y)
      for (int x = int(std::ceil(x0)); x < int(std::floor(x1)); ++x)
        if (const QColor c = image.pixelColor(x, y); c != bg && c != currentLine)
          return true;
    return false;
  }

  void tabsFollowTheCellGrid() {
    QQuickView view(&m_engine, nullptr);
    view.setSource(QUrl::fromLocalFile(QFINDTESTDATA("editor.qml")));
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    auto *editor = qobject_cast<CodeEditor *>(view.rootObject());
    editor->setCursorBlinkInterval(0);
    editor->setText(QStringLiteral("\tM"));
    editor->setCursorPosition(editor->document()->length()); // keep the cursor bar out of the columns checked
    const int lh = int(editor->metrics().lineHeight());
    const qreal adv = editor->metrics().cellAdvance();
    QCOMPARE(editor->tabWidth(), 4);
    for (int tab : {4, 2, 8}) {
      editor->setTabWidth(tab);
      QTRY_VERIFY(inkInColumns(view.grabWindow(), tab * adv + 1, (tab + 1) * adv - 1, lh));
      const QImage image = view.grabWindow();
      QVERIFY2(!inkInColumns(image, 1, tab * adv - 1, lh), qPrintable(QString::number(tab)));
      QCOMPARE(editor->rectForPosition(1).left(), tab * adv);
      QCOMPARE(editor->positionAt(tab * adv + 1, 1), 1);
      QCOMPARE(editor->positionAt(tab * adv / 2 - 1, 1), 0);
    }
  }

  void whitespaceMarksAreOptionalAndDim() {
    QQuickView view(&m_engine, nullptr);
    view.setSource(QUrl::fromLocalFile(QFINDTESTDATA("editor.qml")));
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    auto *editor = qobject_cast<CodeEditor *>(view.rootObject());
    editor->setCursorBlinkInterval(0);
    editor->setText(QStringLiteral("M M"));
    editor->setCursorPosition(editor->document()->length()); // keep the cursor bar out of the columns checked
    const int lh = int(editor->metrics().lineHeight());
    const qreal adv = editor->metrics().cellAdvance();
    QVERIFY(!editor->showWhitespace());
    QTRY_VERIFY(!inkInColumns(view.grabWindow(), adv + 1, 2 * adv - 1, lh));
    editor->setShowWhitespace(true);
    QTRY_VERIFY(inkInColumns(view.grabWindow(), adv + 1, 2 * adv - 1, lh));
    // The mark uses the whitespace color, much dimmer than the text.
    const QImage image = view.grabWindow();
    int brightest = 0;
    for (int y = 0; y < lh; ++y)
      for (int x = int(adv) + 1; x < int(2 * adv) - 1; ++x)
        brightest = qMax(brightest, image.pixelColor(x, y).red());
    QVERIFY2(brightest <= 0x60, qPrintable(QString::number(brightest)));
    // Tabs get a mark across their span.
    editor->setText(QStringLiteral("\tM"));
    editor->setCursorPosition(editor->document()->length()); // keep the cursor bar out of the columns checked
    QTRY_VERIFY(inkInColumns(view.grabWindow(), 1, 4 * adv - 1, lh));
    editor->setShowWhitespace(false);
    QTRY_VERIFY(!inkInColumns(view.grabWindow(), 1, 4 * adv - 1, lh));
    editor->setText(QStringLiteral("M M"));
    editor->setCursorPosition(editor->document()->length()); // keep the cursor bar out of the columns checked
    QTRY_VERIFY(!inkInColumns(view.grabWindow(), adv + 1, 2 * adv - 1, lh));
  }

  // ---- Editing (M3) ----

private:
  static void typeText(QWindow *window, const char *text) {
    for (const char *c = text; *c; ++c)
      QTest::keyClick(window, *c);
  }
  // A shown, focused editor to send key events to.
  struct Shown {
    std::unique_ptr<QQuickView> view;
    CodeEditor *editor = nullptr;
  };
  Shown showEditor() {
    Shown shown;
    shown.view = std::make_unique<QQuickView>(&m_engine, nullptr);
    shown.view->setSource(QUrl::fromLocalFile(QFINDTESTDATA("editor.qml")));
    shown.view->show();
    if (!QTest::qWaitForWindowExposed(shown.view.get()))
      return {};
    shown.editor = qobject_cast<CodeEditor *>(shown.view->rootObject());
    shown.editor->forceActiveFocus();
    shown.editor->setCursorBlinkInterval(0);
    return shown;
  }

private slots:
  void typesAndDeletes() {
    auto [view, editor] = showEditor();
    QVERIFY(editor);
    typeText(view.get(), "hello");
    QCOMPARE(editor->document()->rope().toString(), QStringLiteral("hello"));
    QCOMPARE(editor->cursorPosition(), 5);
    QTest::keyClick(view.get(), Qt::Key_Backspace);
    QTest::keyClick(view.get(), Qt::Key_Return);
    typeText(view.get(), "x");
    QCOMPARE(editor->document()->rope().toString(), QStringLiteral("hell\nx"));
    QCOMPARE(editor->lineCount(), 2);
    QTest::keyClick(view.get(), Qt::Key_Home, Qt::ControlModifier);
    QTest::keyClick(view.get(), Qt::Key_Delete);
    QCOMPARE(editor->document()->rope().toString(), QStringLiteral("ell\nx"));
  }

  void undoRedoAndState() {
    auto [view, editor] = showEditor();
    QVERIFY(editor);
    QVERIFY(!editor->canUndo());
    QSignalSpy undoSpy(editor, &CodeEditor::canUndoChanged);
    typeText(view.get(), "abc");
    QVERIFY(editor->canUndo());
    QCOMPARE(undoSpy.count(), 1);
    QTest::keyClick(view.get(), Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(editor->document()->rope().toString(), QString());
    QVERIFY(!editor->canUndo());
    QVERIFY(editor->canRedo());
    QTest::keyClick(view.get(), Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(editor->document()->rope().toString(), QStringLiteral("abc"));
    QCOMPARE(editor->cursorPosition(), 3);
  }

  void readOnlyRefusesUserEdits() {
    auto [view, editor] = showEditor();
    QVERIFY(editor);
    editor->setText(QStringLiteral("abc"));
    editor->setReadOnly(true);
    typeText(view.get(), "x");
    QTest::keyClick(view.get(), Qt::Key_Delete);
    editor->insert(QStringLiteral("y"));
    QCOMPARE(editor->document()->rope().toString(), QStringLiteral("abc"));
    QTest::keyClick(view.get(), Qt::Key_Right);
    QCOMPARE(editor->cursorPosition(), 1);
  }

  void cursorScrollsIntoView() {
    auto [view, editor] = showEditor();
    QVERIFY(editor);
    QString text;
    for (int i = 0; i < 200; ++i)
      text += QStringLiteral("line %1\n").arg(i);
    editor->setText(text);
    QCOMPARE(editor->contentY(), 0.0);
    QTest::keyClick(view.get(), Qt::Key_End, Qt::ControlModifier);
    const qreal lh = editor->metrics().lineHeight();
    QCOMPARE(editor->contentY(), editor->contentHeight() - editor->height());
    QVERIFY(editor->rectForPosition(editor->cursorPosition()).bottom() <= editor->height() + 0.5);
    // Scrolling away by hand is not undone until the next command.
    editor->setContentY(0);
    QCOMPARE(editor->contentY(), 0.0);
    QTest::keyClick(view.get(), Qt::Key_Up);
    QVERIFY(editor->contentY() > 0);
    QTest::keyClick(view.get(), Qt::Key_Home, Qt::ControlModifier);
    QCOMPARE(editor->contentY(), 0.0);
    // Page down moves the cursor and the view together.
    QTest::keyClick(view.get(), Qt::Key_PageDown);
    const qreal rowOnScreen = editor->rectForPosition(editor->cursorPosition()).top();
    QVERIFY(editor->contentY() > 0);
    QVERIFY(rowOnScreen >= 0 && rowOnScreen < lh * 2);
  }

  void clipboardRoundTrip() {
    auto [view, editor] = showEditor();
    QVERIFY(editor);
    editor->setText(QStringLiteral("hello world"));
    editor->select(0, 5);
    editor->copy();
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("hello"));
    editor->setCursorPosition(11);
    editor->paste();
    QCOMPARE(editor->document()->rope().toString(), QStringLiteral("hello worldhello"));
    editor->select(0, 6);
    editor->cut();
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("hello "));
    QCOMPARE(editor->document()->rope().toString(), QStringLiteral("worldhello"));
    QTest::keyClick(view.get(), Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(editor->document()->rope().toString(), QStringLiteral("hello worldhello"));
    QCOMPARE(editor->selectionEnd(), 6);
  }

  void largePasteAndCopyRunOffTheGuiThread() {
    auto [view, editor] = showEditor();
    QVERIFY(editor);
    const QString big = QString(1500000, u'x') + QStringLiteral("\nend");
    QGuiApplication::clipboard()->setText(big);
    editor->setText(QStringLiteral("[]"));
    editor->setCursorPosition(1);
    editor->paste();
    // The paste lands when the worker is done; meanwhile typing moves the target range along.
    editor->document()->insert(0, u"<");
    QTRY_COMPARE(editor->document()->length(), big.size() + 3);
    const QString text = editor->document()->rope().toString();
    QVERIFY(text.startsWith(QStringLiteral("<[x")));
    QVERIFY(text.endsWith(QStringLiteral("end]")));
    QVERIFY(editor->canUndo());
    // Copy goes through a worker too.
    QGuiApplication::clipboard()->clear();
    editor->selectAll();
    editor->copy();
    QTRY_COMPARE(QGuiApplication::clipboard()->text().size(), big.size() + 3);
  }

  // The pixel at the middle of the character cell `column` on `row`, in window coordinates.
  static QPoint cellPoint(CodeEditor *editor, int row, int column, bool left = false) {
    const auto &m = editor->metrics();
    // Offsets round to the nearest boundary, so aim at the boundary itself for exact columns.
    return QPoint(int(column * m.cellAdvance() + (left ? 0 : 0) + 1), int((row + 0.5) * m.lineHeight()));
  }

  void clickPlacesCursorAndDragSelects() {
    auto [view, editor] = showEditor();
    QVERIFY(editor);
    editor->setText(QStringLiteral("hello world\nsecond line"));
    QTest::mouseClick(view.get(), Qt::LeftButton, {}, cellPoint(editor, 0, 4), 10);
    QCOMPARE(editor->cursorPosition(), 4);
    QTest::mouseClick(view.get(), Qt::LeftButton, {}, cellPoint(editor, 1, 3), 10);
    QCOMPARE(editor->cursorPosition(), 12 + 3);
    QCOMPARE(editor->selectionStart(), editor->selectionEnd());

    QTest::mousePress(view.get(), Qt::LeftButton, {}, cellPoint(editor, 0, 2), 10);
    QTest::mouseMove(view.get(), cellPoint(editor, 1, 5));
    QTest::mouseRelease(view.get(), Qt::LeftButton, {}, cellPoint(editor, 1, 5), 10);
    QCOMPARE(editor->selectionStart(), 2);
    QCOMPARE(editor->selectionEnd(), 12 + 5);
    QCOMPARE(editor->cursorPosition(), 12 + 5); // the head follows the pointer

    // Dragging backwards keeps the anchor and flips the head.
    QTest::mousePress(view.get(), Qt::LeftButton, {}, cellPoint(editor, 1, 5), 10);
    QTest::mouseMove(view.get(), cellPoint(editor, 0, 1));
    QTest::mouseRelease(view.get(), Qt::LeftButton, {}, cellPoint(editor, 0, 1), 10);
    QCOMPARE(editor->cursorPosition(), 1);
    QCOMPARE(editor->selectionEnd(), 12 + 5);
  }

  void shiftClickExtends() {
    auto [view, editor] = showEditor();
    QVERIFY(editor);
    editor->setText(QStringLiteral("0123456789"));
    QTest::mouseClick(view.get(), Qt::LeftButton, {}, cellPoint(editor, 0, 2), 10);
    QTest::mouseClick(view.get(), Qt::LeftButton, Qt::ShiftModifier, cellPoint(editor, 0, 7), 10);
    QCOMPARE(editor->selectionStart(), 2);
    QCOMPARE(editor->selectionEnd(), 7);
    QTest::mouseClick(view.get(), Qt::LeftButton, Qt::ShiftModifier, cellPoint(editor, 0, 0), 10);
    QCOMPARE(editor->selectionStart(), 0);
    QCOMPARE(editor->selectionEnd(), 2); // the anchor stays where the first click put it
  }

  void doubleClickSelectsWordTripleClickLine() {
    auto [view, editor] = showEditor();
    QVERIFY(editor);
    editor->setText(QStringLiteral("foo bar_baz qux\nnext"));
    const QPoint p = cellPoint(editor, 0, 6);
    QTest::mouseDClick(view.get(), Qt::LeftButton, {}, p, 10); // press, release, double-click press, release
    QCOMPARE(editor->selectionStart(), 4);
    QCOMPARE(editor->selectionEnd(), 11);
    // A third click quickly after selects the whole line, including its break.
    QTest::mouseClick(view.get(), Qt::LeftButton, {}, p, 10);
    QCOMPARE(editor->selectionStart(), 0);
    QCOMPARE(editor->selectionEnd(), 16);
  }

  void wordDragGrowsByWords() {
    auto [view, editor] = showEditor();
    QVERIFY(editor);
    editor->setText(QStringLiteral("aaa bbb ccc ddd"));
    const QPoint p = cellPoint(editor, 0, 5);
    QTest::mouseClick(view.get(), Qt::LeftButton, {}, p, 10);
    QTest::mousePress(view.get(), Qt::LeftButton, {}, p, 10); // a second quick press counts as a double click
    QCOMPARE(editor->selectionStart(), 4);
    QCOMPARE(editor->selectionEnd(), 7);
    QTest::mouseMove(view.get(), cellPoint(editor, 0, 13));
    QCOMPARE(editor->selectionStart(), 4);
    QCOMPARE(editor->selectionEnd(), 15); // grown to the end of "ddd"
    QTest::mouseMove(view.get(), cellPoint(editor, 0, 1));
    QCOMPARE(editor->cursorPosition(), 0); // and backwards to the start of "aaa", keeping "bbb"
    QCOMPARE(editor->selectionEnd(), 7);
    QTest::mouseRelease(view.get(), Qt::LeftButton, {}, cellPoint(editor, 0, 1), 10);
  }

  void clickFocusesAndBreaksTypingRun() {
    auto [view, editor] = showEditor();
    QVERIFY(editor);
    editor->setText(QStringLiteral("ab"));
    typeText(view.get(), "x");
    QTest::mouseClick(view.get(), Qt::LeftButton, {}, cellPoint(editor, 0, 3), 10);
    typeText(view.get(), "y");
    QCOMPARE(editor->document()->undoStack().undoSteps(), 2);
  }

  void dragOutsideAutoScrolls() {
    auto [view, editor] = showEditor();
    QVERIFY(editor);
    QString text;
    for (int i = 0; i < 300; ++i)
      text += QStringLiteral("line %1\n").arg(i);
    editor->setText(text);
    QTest::mousePress(view.get(), Qt::LeftButton, {}, cellPoint(editor, 0, 1), 10);
    QTest::mouseMove(view.get(), QPoint(10, int(editor->height()) + 30));
    QTRY_VERIFY(editor->contentY() > 100);
    const qsizetype head = editor->cursorPosition();
    QTRY_VERIFY(editor->cursorPosition() > head);
    QTest::mouseRelease(view.get(), Qt::LeftButton, {}, QPoint(10, int(editor->height()) + 30), 10);
    const qreal y = editor->contentY();
    QTest::qWait(100);
    QCOMPARE(editor->contentY(), y); // releasing stops the scrolling
  }

  void unfocusedEditorShowsNoCursorAndStopsBlinking() {
    auto [view, editor] = showEditor();
    QVERIFY(editor);
    editor->setText(QStringLiteral("MMMM"));
    editor->setCursorPosition(2);
    editor->setCursorBlinkInterval(20);
    QTRY_VERIFY(editor->hasFocus());
    QSignalSpy spy(editor, &CodeEditor::cursorVisibleChanged);
    QTRY_VERIFY(spy.count() >= 2); // blinking while focused
    editor->setFocus(false);
    QTRY_VERIFY(!editor->hasFocus());
    spy.clear();
    QTest::qWait(100);
    QCOMPARE(spy.count(), 0); // the timer is stopped: an idle editor renders nothing
    const QImage image = view->grabWindow();
    const qreal adv = editor->metrics().cellAdvance();
    QVERIFY(image.pixelColor(int(adv * 2 + 1), 2) != editor->theme()->cursor());
    editor->forceActiveFocus();
    QTRY_VERIFY(editor->hasFocus());
    QTRY_VERIFY(spy.count() >= 2);
  }

  void focusLossEndsTypingRunAndTabFocusIsEnabled() {
    auto [view, editor] = showEditor();
    QVERIFY(editor);
    QVERIFY(editor->activeFocusOnTab());
    typeText(view.get(), "a");
    editor->setFocus(false);
    editor->forceActiveFocus();
    typeText(view.get(), "b");
    QCOMPARE(editor->document()->undoStack().undoSteps(), 2);
  }

  // ---- Input methods ----

  static void sendIme(CodeEditor *editor, const QString &preedit, const QString &commit = {},
                      int cursor = -1, int replaceStart = 0, int replaceLength = 0) {
    QList<QInputMethodEvent::Attribute> attributes;
    if (cursor >= 0)
      attributes.append({QInputMethodEvent::Cursor, cursor, 1, QVariant()});
    QInputMethodEvent event(preedit, attributes);
    event.setCommitString(commit, replaceStart, replaceLength);
    QCoreApplication::sendEvent(editor, &event);
  }

  void inputMethodCommitInsertsText() {
    auto [view, editor] = showEditor();
    QVERIFY(editor);
    QVERIFY(editor->flags() & QQuickItem::ItemAcceptsInputMethod);
    editor->setText(QStringLiteral("ab"));
    editor->setCursorPosition(1);
    sendIme(editor, {}, QStringLiteral("日本"));
    QCOMPARE(editor->document()->rope().toString(), QStringLiteral("a日本b"));
    QCOMPARE(editor->cursorPosition(), 3);
    // A commit can replace text around the cursor (as a dead key or predictive input does).
    sendIme(editor, {}, QStringLiteral("X"), -1, -2, 2);
    QCOMPARE(editor->document()->rope().toString(), QStringLiteral("aXb"));
    QCOMPARE(editor->cursorPosition(), 2);
  }

  void preeditIsShownButNotInTheDocument() {
    auto [view, editor] = showEditor();
    QVERIFY(editor);
    editor->setText(QStringLiteral("ab\ncd"));
    editor->setCursorPosition(1);
    const QRectF before = editor->inputMethodQuery(Qt::ImCursorRectangle).toRectF();
    const quint64 version = editor->document()->version();
    sendIme(editor, QStringLiteral("nihao"), {}, 3);
    QCOMPARE(editor->document()->version(), version);
    QCOMPARE(editor->document()->rope().toString(), QStringLiteral("ab\ncd"));
    QCOMPARE(editor->cursorPosition(), 1);
    // The candidate window anchors at the cursor inside the composition: 3 cells to the right.
    const QRectF during = editor->inputMethodQuery(Qt::ImCursorRectangle).toRectF();
    QVERIFY(qAbs(during.left() - before.left() - 3 * editor->metrics().cellAdvance()) < 1.0);
    QCOMPARE(editor->inputMethodQuery(Qt::ImSurroundingText).toString(), QStringLiteral("ab"));
    QCOMPARE(editor->inputMethodQuery(Qt::ImCursorPosition).toInt(), 1);
    // It is drawn: the composition's underline puts ink on the cells it covers.
    const qreal adv = editor->metrics().cellAdvance();
    QTRY_VERIFY(inkInColumns(view->grabWindow(), adv * 1 + 1, adv * 6 - 1, int(editor->metrics().lineHeight())));
    // Committing replaces it with real text.
    sendIme(editor, {}, QStringLiteral("你好"));
    QCOMPARE(editor->document()->rope().toString(), QStringLiteral("a你好b\ncd"));
    QCOMPARE(editor->cursorPosition(), 3);
    QVERIFY(editor->inputMethodQuery(Qt::ImCursorRectangle).toRectF().left() > before.left());
  }

  void clickingEndsTheComposition() {
    auto [view, editor] = showEditor();
    QVERIFY(editor);
    editor->setText(QStringLiteral("abcdef"));
    editor->setCursorPosition(2);
    sendIme(editor, QStringLiteral("xyz"));
    QTest::mouseClick(view.get(), Qt::LeftButton, {}, cellPoint(editor, 0, 0), 10);
    // Nothing was committed; the composition is gone and hit-testing is back to plain text.
    QCOMPARE(editor->document()->rope().toString(), QStringLiteral("abcdef"));
    QCOMPARE(editor->cursorPosition(), 0);
    QCOMPARE(editor->positionAt(editor->metrics().cellAdvance() * 4 + 1, 1), 4);
  }

  void preeditHitTestingSkipsTheComposition() {
    auto [view, editor] = showEditor();
    QVERIFY(editor);
    editor->setText(QStringLiteral("abcdef"));
    editor->setCursorPosition(2);
    sendIme(editor, QStringLiteral("XYZ"));
    const qreal adv = editor->metrics().cellAdvance();
    QCOMPARE(editor->positionAt(adv * 1 + 1, 1), 1);
    QCOMPARE(editor->positionAt(adv * 3 + 1, 1), 2);  // inside the composition: before it
    QCOMPARE(editor->positionAt(adv * 6 + 1, 1), 3);  // "c" is now at cells 5..6
  }

  void inputMethodIgnoredWhenReadOnly() {
    auto [view, editor] = showEditor();
    QVERIFY(editor);
    editor->setText(QStringLiteral("ab"));
    editor->setReadOnly(true);
    QVERIFY(!editor->inputMethodQuery(Qt::ImEnabled).toBool());
    sendIme(editor, QStringLiteral("x"), QStringLiteral("y"));
    QCOMPARE(editor->document()->rope().toString(), QStringLiteral("ab"));
  }

  void savesInTheBackgroundAndReportsPosition() {
    auto [view, editor] = showEditor();
    QVERIFY(editor);
    editor->setText(QStringLiteral("one\ntwo"));
    editor->setCursorPosition(6);
    QCOMPARE(editor->cursorLine(), 1);
    QCOMPARE(editor->cursorColumn(), 2);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("out.txt"));
    QSignalSpy saved(editor, &CodeEditor::saved);
    editor->save(QUrl::fromLocalFile(path));
    QTRY_COMPARE(saved.count(), 1);
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("one\ntwo"));
    QSignalSpy failed(editor, &CodeEditor::saveFailed);
    editor->save(QUrl::fromLocalFile(dir.filePath(QStringLiteral("missing/dir/out.txt"))));
    QTRY_COMPARE(failed.count(), 1);
  }

  void setTextUpdatesLineCount() {
    CodeEditor editor;
    QSignalSpy spy(&editor, &CodeEditor::lineCountChanged);
    editor.setText(QStringLiteral("a\nb\nc"));
    QCOMPARE(editor.lineCount(), 3);
    QVERIFY(spy.count() >= 1);
  }
};

QTEST_MAIN(TstCodeEditor)
#include "tst_codeeditor.moc"
