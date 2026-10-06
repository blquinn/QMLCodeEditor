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
#include "quick/decorationcolumn.h"
#include "quick/popupplacement.h"

Q_IMPORT_QML_PLUGIN(me_blq_qmlcodeeditorPlugin)

namespace {

QVariantMap lsp(int line, int from, int to, int severity, const QString &message) {
  return {{"range", QVariantMap{{"start", QVariantMap{{"line", line}, {"character", from}}},
                                {"end", QVariantMap{{"line", line}, {"character", to}}}}},
          {"severity", severity}, {"message", message}, {"source", "test"}, {"code", 7}};
}

} // namespace

class TstPopup : public QObject {
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
    shown.editor->setHoverDelay(20);
    return shown;
  }
  // Items are found through their visual parents; the popup's QObject parent is not the content item.
  static QQuickItem *findItem(QQuickItem *root, const QString &name) {
    for (QQuickItem *child : root->childItems()) {
      if (child->objectName() == name)
        return child;
      if (QQuickItem *found = findItem(child, name))
        return found;
    }
    return nullptr;
  }
  static QQuickItem *popupItem(QQuickView *view, const char *name = "diagnosticPopup") {
    return findItem(view->contentItem(), QString::fromLatin1(name));
  }
  static QPoint charCenter(CodeEditor *editor, int row, int column) {
    const qreal cell = editor->metrics().cellAdvance(), lh = editor->metrics().lineHeight();
    return QPoint(int(editor->gutterWidth() + (column + 0.5) * cell), int((row + 0.5) * lh));
  }

private slots:
  void initTestCase() { QQuickWindow::setGraphicsApi(QSGRendererInterface::Software); }

  void placementPrefersBelowThenAboveThenTheRoomierSide_data() {
    QTest::addColumn<QRectF>("anchor");
    QTest::addColumn<QSizeF>("size");
    QTest::addColumn<QPointF>("expected");
    const QRectF bounds(0, 0, 400, 300);
    Q_UNUSED(bounds);
    // bounds are always 400x300 here; the gap is 2.
    QTest::newRow("below, left edges line up") << QRectF(100, 50, 8, 16) << QSizeF(120, 60) << QPointF(100, 68);
    QTest::newRow("flips above at the bottom") << QRectF(100, 270, 8, 16) << QSizeF(120, 60) << QPointF(100, 208);
    QTest::newRow("pushed left at the right edge") << QRectF(380, 50, 8, 16) << QSizeF(120, 60) << QPointF(280, 68);
    QTest::newRow("wider than the bounds starts at the left") << QRectF(100, 50, 8, 16) << QSizeF(500, 60) << QPointF(0, 68);
    QTest::newRow("exactly fitting below stays below") << QRectF(0, 222, 8, 16) << QSizeF(50, 60) << QPointF(0, 240);
    QTest::newRow("one pixel too tall for below flips") << QRectF(0, 223, 8, 16) << QSizeF(50, 60) << QPointF(0, 161);
    // 300 tall, the anchor in the middle: neither side holds 200; the roomier (below) wins and is pushed up.
    QTest::newRow("fits neither: roomier side, pushed inside") << QRectF(0, 140, 8, 16) << QSizeF(50, 200) << QPointF(0, 100);
    QTest::newRow("fits neither, more room above") << QRectF(0, 200, 8, 16) << QSizeF(50, 280) << QPointF(0, 0);
    QTest::newRow("taller than the bounds") << QRectF(0, 100, 8, 16) << QSizeF(50, 400) << QPointF(0, 0);
    QTest::newRow("anchor left of the bounds") << QRectF(-30, 50, 8, 16) << QSizeF(50, 20) << QPointF(0, 68);
  }
  void placementPrefersBelowThenAboveThenTheRoomierSide() {
    QFETCH(QRectF, anchor);
    QFETCH(QSizeF, size);
    QFETCH(QPointF, expected);
    const QPointF got = qce::placePopup(anchor, size, QRectF(0, 0, 400, 300));
    QCOMPARE(got, expected);
    // Whatever the case, a popup that fits the bounds is inside them.
    if (size.width() <= 400 && size.height() <= 300) {
      QVERIFY(got.x() >= 0 && got.y() >= 0);
      QVERIFY(got.x() + size.width() <= 400 && got.y() + size.height() <= 300);
    }
  }

  void hoveringADiagnosticShowsThePopupBelowItAndLeavingHidesIt() {
    auto [view, editor] = showEditor();
    editor->setText(QStringLiteral("let a = 1;\nlet b = oops;\nlet c = 3;"));
    editor->setDiagnostics(QVariantList{lsp(1, 8, 12, 1, "oops is not defined")});
    QVERIFY(!editor->popupVisible());
    QTest::mouseMove(view.get(), charCenter(editor, 1, 9));
    QTRY_VERIFY(editor->popupVisible());
    QQuickItem *popup = popupItem(view.get());
    QVERIFY(popup);
    const qreal lh = editor->metrics().lineHeight(), cell = editor->metrics().cellAdvance();
    QVERIFY(popup->y() >= 2 * lh); // below the row
    QCOMPARE(popup->x(), 9 * cell);  // the hovered character's left edge
    QVERIFY(popup->width() > 0 && popup->height() > 0);
    QCOMPARE(popup->parentItem(), view->contentItem());
    // The default popup shows the message.
    QVERIFY(!popup->childItems().isEmpty());
    // Leaving the diagnostic closes it once the grace period has passed.
    QTest::mouseMove(view.get(), QPoint(380, 190));
    QTRY_VERIFY_WITH_TIMEOUT(!editor->popupVisible(), 2000);
    QTRY_VERIFY(!popupItem(view.get()));
  }

  void textWithoutADiagnosticShowsNothing() {
    auto [view, editor] = showEditor();
    editor->setText(QStringLiteral("let a = 1;\nlet b = oops;"));
    editor->setDiagnostics(QVariantList{lsp(1, 8, 12, 1, "x")});
    QTest::mouseMove(view.get(), charCenter(editor, 0, 3));
    QTest::qWait(150);
    QVERIFY(!editor->popupVisible());
    QTest::mouseMove(view.get(), charCenter(editor, 1, 13)); // just past the range
    QTest::qWait(150);
    QVERIFY(!editor->popupVisible());
  }

  void movingToAnotherDiagnosticReplacesThePopup() {
    auto [view, editor] = showEditor();
    // The second diagnostic is on the same row, far enough right not to be under the first popup.
    editor->setText(QStringLiteral("aaaa") + QString(26, u' ') + QStringLiteral("bbbb\nnext line"));
    editor->setDiagnostics(QVariantList{lsp(0, 0, 4, 2, "first"), lsp(0, 30, 34, 1, "second")});
    QTest::mouseMove(view.get(), charCenter(editor, 0, 1));
    QTRY_VERIFY(editor->popupVisible());
    QQuickItem *first = popupItem(view.get());
    QVERIFY(first);
    const qreal firstX = first->x();
    QTest::mouseMove(view.get(), charCenter(editor, 0, 31));
    QTRY_VERIFY(popupItem(view.get()) && popupItem(view.get())->x() > firstX);
    QVERIFY(editor->popupVisible());
    QCOMPARE(popupItem(view.get())->x(), 31 * editor->metrics().cellAdvance()); // the hovered character
    // A pointer that lands on the popup keeps it, even over other diagnostics' text.
    editor->hidePopup();
    QTest::mouseMove(view.get(), QPoint(380, 190));
    editor->setDiagnostics(QVariantList{lsp(0, 0, 4, 2, "first"), lsp(1, 0, 4, 1, "under it")});
    QTest::mouseMove(view.get(), charCenter(editor, 0, 1));
    QTRY_VERIFY(editor->popupVisible());
    QTest::mouseMove(view.get(), charCenter(editor, 1, 1)); // under the popup
    QTest::qWait(400);
    QVERIFY(editor->popupVisible());
  }

  void thePopupFlipsAboveNearTheBottomOfTheWindow() {
    auto [view, editor] = showEditor(100, 400);
    QString text;
    for (int i = 0; i < 20; ++i)
      text += QStringLiteral("line %1\n").arg(i);
    editor->setText(text);
    const int lastRow = int(100 / editor->metrics().lineHeight()) - 1;
    editor->setDiagnostics(QVariantList{lsp(lastRow, 0, 4, 1, "near the bottom")});
    QTest::mouseMove(view.get(), charCenter(editor, lastRow, 1));
    QTRY_VERIFY(editor->popupVisible());
    QQuickItem *popup = popupItem(view.get());
    const qreal rowTop = lastRow * editor->metrics().lineHeight();
    QVERIFY(popup->y() + popup->height() <= rowTop); // above the row
    QVERIFY(popup->y() >= 0);
  }

  void aCustomDelegateGetsTheDiagnosticsAndTheEditor() {
    auto [view, editor] = showEditor();
    editor->setText(QStringLiteral("abc def"));
    editor->setDiagnostics(QVariantList{lsp(0, 0, 3, 1, "one"), lsp(0, 1, 2, 2, "two")});
    QQmlComponent component(&m_engine);
    component.setData(
      "import QtQuick\n"
      "Item {\n"
      "  objectName: 'custom'\n"
      "  required property var diagnostics\n"
      "  required property Item editor\n"
      "  readonly property int count: diagnostics.length\n"
      "  readonly property string first: diagnostics[0].message\n"
      "  readonly property bool sameEditor: editor.objectName === 'theEditor'\n"
      "  implicitWidth: 80; implicitHeight: 30\n"
      "}\n",
      QUrl());
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    editor->setObjectName("theEditor");
    editor->setPopupDelegate(&component);
    QVERIFY(editor->showDiagnosticsAt(1));
    QQuickItem *popup = popupItem(view.get(), "custom");
    QVERIFY(popup);
    QCOMPARE(popup->property("count").toInt(), 2);
    QCOMPARE(popup->property("first").toString(), QStringLiteral("one")); // the most severe first
    QVERIFY(popup->property("sameEditor").toBool());
    QCOMPARE(popup->size(), QSizeF(80, 30)); // from the implicit size
    editor->setPopupDelegate(nullptr);
    QVERIFY(!editor->popupVisible()); // swapping the delegate closes it
    QVERIFY(!editor->showDiagnosticsAt(5)); // nothing at "e" -- the second diagnostic's range ends at 2
  }

  void thePopupClosesOnKeysEditsClicksAndScrolls() {
    auto [view, editor] = showEditor(100, 400);
    QString text;
    for (int i = 0; i < 40; ++i)
      text += QStringLiteral("line %1\n").arg(i);
    editor->setText(text);
    editor->setDiagnostics(QVariantList{lsp(0, 0, 4, 1, "x")});
    auto open = [&] {
      QVERIFY(editor->showDiagnosticsAt(1));
      QVERIFY(editor->popupVisible());
    };
    open();
    QTest::keyClick(view.get(), Qt::Key_Right);
    QVERIFY(!editor->popupVisible());
    open();
    editor->document()->insert(0, QStringLiteral("z"));
    QVERIFY(!editor->popupVisible());
    open();
    QTest::mouseClick(view.get(), Qt::LeftButton, {}, charCenter(editor, 3, 2));
    QVERIFY(!editor->popupVisible());
    open();
    editor->setContentY(40);
    QVERIFY(!editor->popupVisible());
    editor->setContentY(0);
    open();
    editor->clearDiagnostics(); // what it showed is gone
    QVERIFY(!editor->popupVisible());
  }

  void thePopupStaysWhileThePointerIsOnIt() {
    auto [view, editor] = showEditor();
    editor->setText(QStringLiteral("let a = 1;\nlet b = oops;\nlet c = 3;"));
    editor->setDiagnostics(QVariantList{lsp(1, 8, 12, 1, "oops is not defined")});
    QTest::mouseMove(view.get(), charCenter(editor, 1, 9));
    QTRY_VERIFY(editor->popupVisible());
    QQuickItem *popup = popupItem(view.get());
    const QPoint inside = popup->mapToScene(QPointF(popup->width() / 2, popup->height() / 2)).toPoint();
    QTest::mouseMove(view.get(), inside);
    QTest::qWait(600); // well past the grace period
    QVERIFY(editor->popupVisible());
    QTest::mouseMove(view.get(), QPoint(380, 190));
    QTRY_VERIFY_WITH_TIMEOUT(!editor->popupVisible(), 2000);
  }

  void thePopupTextCanBeSelectedAndCopied() {
    auto [view, editor] = showEditor();
    editor->setText(QStringLiteral("let a = 1;\nlet b = oops;\nlet c = 3;"));
    editor->setDiagnostics(QVariantList{lsp(1, 8, 12, 1, "oops is not defined")});
    QTest::mouseMove(view.get(), charCenter(editor, 1, 9));
    QTRY_VERIFY(editor->popupVisible());
    QQuickItem *popup = popupItem(view.get());
    QQuickItem *message = findItem(popup, QStringLiteral("diagnosticMessage"));
    QVERIFY(message);
    QCOMPARE(message->property("readOnly").toBool(), true);
    QCOMPARE(message->property("selectByMouse").toBool(), true);
    // A double-click on a word selects it and gives the popup the keyboard.
    const QPoint inMessage = message->mapToScene(QPointF(8, message->height() / 2)).toPoint();
    QTest::mouseMove(view.get(), inMessage);
    QTest::mouseDClick(view.get(), Qt::LeftButton, {}, inMessage);
    QTRY_VERIFY(message->hasActiveFocus());
    QCOMPARE(message->property("selectedText").toString(), QStringLiteral("oops"));
    // Moving away does not close a popup that holds the focus (the selection may be wanted).
    QTest::mouseMove(view.get(), QPoint(380, 190));
    QTest::qWait(500);
    QVERIFY(editor->popupVisible());
    // Copy.
    QGuiApplication::clipboard()->clear();
    QTest::keyClick(view.get(), Qt::Key_C, Qt::ControlModifier);
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("oops"));
    QVERIFY(editor->popupVisible()); // the keys went to the popup, not the editor
    // Escape closes it and returns the keyboard to the editor.
    QTest::keyClick(view.get(), Qt::Key_Escape);
    QVERIFY(!editor->popupVisible());
    QTRY_VERIFY(editor->hasActiveFocus());
    // A click in the editor also closes it.
    QTest::mouseMove(view.get(), charCenter(editor, 1, 9));
    QTRY_VERIFY(editor->popupVisible());
    QTest::mouseClick(view.get(), Qt::LeftButton, {}, charCenter(editor, 0, 1));
    QVERIFY(!editor->popupVisible());
  }

  void theGutterIconAndTheEndOfLineMessageAlsoOpenIt() {
    auto [view, editor] = showEditor();
    editor->setText(QStringLiteral("let a = 1;\nlet b = oops;\nlet c = 3;"));
    auto *column = new qce::DecorationColumn(editor);
    editor->addGutterColumn(column);
    QTRY_VERIFY(editor->gutterWidth() > 0);
    editor->setDiagnostics(QVariantList{lsp(1, 8, 12, 1, "first"), lsp(1, 0, 3, 2, "second")});
    const int lh = int(editor->metrics().lineHeight());
    QTest::mouseMove(view.get(), QPoint(int(editor->gutterWidth() / 2), lh + lh / 2));
    QTRY_VERIFY(editor->popupVisible());
    QVERIFY(popupItem(view.get()));
    // Both diagnostics of the line, the more severe first.
    editor->hidePopup();
    QTest::mouseMove(view.get(), QPoint(380, 190));
    editor->setDiagnosticMessages(CodeEditor::EndOfLineMessages);
    QTRY_VERIFY(editor->contentWidth() > 0);
    const qreal cell = editor->metrics().cellAdvance();
    // "let b = oops;" is 13 cells; the message starts two cells after.
    QTest::mouseMove(view.get(), QPoint(int(editor->gutterWidth() + 16 * cell), lh + lh / 2));
    QTRY_VERIFY(editor->popupVisible());
  }

  void navigationShowsThePopupAndItCanBeSwitchedOff() {
    auto [view, editor] = showEditor();
    editor->setText(QStringLiteral("abc\ndef\nghi"));
    editor->setDiagnostics(QVariantList{lsp(1, 1, 3, 1, "here")});
    editor->setCursorPosition(0);
    QVERIFY(editor->gotoNextDiagnostic());
    QVERIFY(editor->popupVisible());
    QVERIFY(popupItem(view.get()));
    editor->hidePopup();
    editor->setDiagnosticPopups(false);
    editor->setCursorPosition(0);
    QVERIFY(editor->gotoNextDiagnostic());
    QVERIFY(!editor->popupVisible());
    QTest::mouseMove(view.get(), charCenter(editor, 1, 1));
    QTest::qWait(150);
    QVERIFY(!editor->popupVisible());
  }
};

QTEST_MAIN(TstPopup)
#include "tst_popup.moc"
