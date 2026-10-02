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
private slots:
  void initTestCase() { QQuickWindow::setGraphicsApi(QSGRendererInterface::Software); }

  void rendersBackground() {
    QQuickView view;
    view.setSource(QUrl::fromLocalFile(QFINDTESTDATA("editor.qml")));
    QCOMPARE(view.status(), QQuickView::Ready);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    auto *editor = qobject_cast<CodeEditor *>(view.rootObject());
    QVERIFY(editor);
    QVERIFY(editor->flags().testFlag(QQuickItem::ItemHasContents));
    const QImage image = view.grabWindow();
    QVERIFY(!image.isNull());
    QCOMPARE(image.pixelColor(10, 10), QColor(0x1e, 0x1e, 0x1e));
  }

  void themeSwitchRepaints() {
    QQuickView view;
    view.setSource(QUrl::fromLocalFile(QFINDTESTDATA("editor.qml")));
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    auto *editor = qobject_cast<CodeEditor *>(view.rootObject());
    QVERIFY(editor && editor->theme());
    qce::Theme light;
    light.assign(*std::unique_ptr<qce::Theme>(qce::Theme::createLight()));
    editor->setTheme(&light);
    QCOMPARE(editor->theme(), &light);
    QTRY_COMPARE(view.grabWindow().pixelColor(10, 10), QColor(0xff, 0xff, 0xff));
    light.setProperty("background", QColor(0x10, 0x20, 0x30));
    QTRY_COMPARE(view.grabWindow().pixelColor(10, 10), QColor(0x10, 0x20, 0x30));
    editor->setTheme(nullptr); // back to the owned default
    QTRY_COMPARE(view.grabWindow().pixelColor(10, 10), QColor(0x1e, 0x1e, 0x1e));
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
    QQuickView view;
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
    QQuickView view;
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
    QQuickView view;
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
    QQuickView view;
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
    QQuickView view;
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
    QQuickView view;
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
