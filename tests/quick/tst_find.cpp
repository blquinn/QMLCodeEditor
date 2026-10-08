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

namespace {

const QColor kRed(0xff, 0x00, 0x00);

bool hasColor(const QImage &image, const QRect &rect, const QColor &color) {
  for (int y = rect.top(); y <= rect.bottom() && y < image.height(); ++y)
    for (int x = rect.left(); x <= rect.right() && x < image.width(); ++x)
      if (image.pixelColor(x, y) == color)
        return true;
  return false;
}

QQuickItem *findItem(QQuickItem *root, const QString &name) {
  for (QQuickItem *child : root->childItems()) {
    if (child->objectName() == name)
      return child;
    if (QQuickItem *found = findItem(child, name))
      return found;
  }
  return nullptr;
}

} // namespace

class TstFind : public QObject {
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
  static QRect cellRect(CodeEditor *editor, int row, int column) {
    const int cell = int(editor->metrics().cellAdvance());
    return QRect(int(editor->gutterWidth()) + column * cell + 1, row * int(editor->metrics().lineHeight()), cell - 2,
                 int(editor->metrics().lineHeight()));
  }

private slots:
  void initTestCase() { QQuickWindow::setGraphicsApi(QSGRendererInterface::Software); }

  void editorHasAFindObject() {
    auto [view, editor] = showEditor();
    QVERIFY(editor->find());
    QVERIFY(!editor->find()->active());
    QCOMPARE(editor->property("find").value<QObject *>(), editor->find());
  }

  void matchesInViewAreMarked() {
    auto [view, editor] = showEditor();
    editor->theme()->setProperty("searchMatch", kRed);
    editor->setText(QStringLiteral("ab ab\ncd"));
    editor->setCursorPosition(0);
    editor->find()->setText(QStringLiteral("ab"));
    QTest::qWait(50);
    QVERIFY(!hasColor(view->grabWindow(), cellRect(editor, 0, 3), kRed)); // not active yet
    editor->find()->setActive(true);
    QTRY_VERIFY(hasColor(view->grabWindow(), cellRect(editor, 0, 3), kRed)); // the second match; the first is selected
    const QImage image = view->grabWindow();
    QVERIFY(!hasColor(image, cellRect(editor, 0, 2), kRed));
    QVERIFY(!hasColor(image, cellRect(editor, 1, 0), kRed));
    editor->find()->setActive(false);
    QTRY_VERIFY(!hasColor(view->grabWindow(), cellRect(editor, 0, 3), kRed));
  }

  void vimAndFindHighlightsCoexist() {
    auto [view, editor] = showEditor();
    editor->theme()->setProperty("searchMatch", kRed);
    editor->setText(QStringLiteral("xx ab ab"));
    editor->setCursorPosition(0);
    editor->setSearchHighlight(QRegularExpression(QStringLiteral("xx")));
    editor->find()->setText(QStringLiteral("ab"));
    editor->find()->setActive(true);
    QTRY_VERIFY(hasColor(view->grabWindow(), cellRect(editor, 0, 6), kRed));
    // The cursor moved to the first "ab"; "xx" is still marked.
    QVERIFY(hasColor(view->grabWindow(), cellRect(editor, 0, 0), kRed));
  }

  void replaceAllEditsTheEditorAsOneUndoStep() {
    auto [view, editor] = showEditor();
    editor->setText(QStringLiteral("a b a"));
    editor->find()->setText(QStringLiteral("a"));
    editor->find()->setReplacement(QStringLiteral("xyz"));
    editor->find()->setActive(true);
    QTRY_COMPARE(editor->find()->matchCount(), 2);
    editor->find()->replaceAll();
    QTRY_COMPARE(editor->document()->rope().toString(), QStringLiteral("xyz b xyz"));
    QTRY_VERIFY(editor->canUndo());
    editor->undo();
    QCOMPARE(editor->document()->rope().toString(), QStringLiteral("a b a"));
  }

  void findBarDrivesTheEditor() {
    auto [view, editor] = showEditor();
    editor->setText(QStringLiteral("one two one two"));
    editor->setCursorPosition(0);
    QQmlComponent component(&m_engine);
    component.setData("import QtQuick\nimport me.blq.qmlcodeeditor\nFindBar {}", QUrl());
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QObject *created = component.createWithInitialProperties(
      {{"editor", QVariant::fromValue(editor)}, {"parent", QVariant::fromValue(editor->parentItem())}}
    );
    QVERIFY2(created, qPrintable(component.errorString()));
    std::unique_ptr<QObject> holder(created);
    auto *bar = qobject_cast<QQuickItem *>(created);
    QVERIFY(bar);
    QVERIFY(!bar->isVisible()); // nothing shows until it is opened

    QVERIFY(QMetaObject::invokeMethod(bar, "open", Q_ARG(QVariant, false)));
    QVERIFY(editor->find()->active());
    QTRY_VERIFY(bar->isVisible());
    QQuickItem *field = findItem(bar, QStringLiteral("findField"));
    QVERIFY(field);
    field->setProperty("text", QStringLiteral("two"));
    QCOMPARE(editor->find()->text(), QStringLiteral("two"));
    QTRY_COMPARE(editor->find()->matchCount(), 2);
    QTRY_COMPARE(editor->selectionStart(), qsizetype(4));

    QTest::keyClick(view.get(), Qt::Key_Return);
    QTRY_COMPARE(editor->selectionStart(), qsizetype(12));
    QTest::keyClick(view.get(), Qt::Key_Return, Qt::ShiftModifier);
    QTRY_COMPARE(editor->selectionStart(), qsizetype(4));

    QTest::keyClick(view.get(), Qt::Key_Escape);
    QVERIFY(!editor->find()->active());
    QVERIFY(!bar->isVisible());
    QTRY_VERIFY(editor->hasActiveFocus());
  }

  void findBarSeedsTheQueryFromTheSelection() {
    auto [view, editor] = showEditor();
    editor->setText(QStringLiteral("alpha beta"));
    editor->select(6, 10);
    QQmlComponent component(&m_engine);
    component.setData("import QtQuick\nimport me.blq.qmlcodeeditor\nFindBar {}", QUrl());
    std::unique_ptr<QObject> holder(component.createWithInitialProperties(
      {{"editor", QVariant::fromValue(editor)}, {"parent", QVariant::fromValue(editor->parentItem())}}
    ));
    QVERIFY(holder);
    QVERIFY(QMetaObject::invokeMethod(holder.get(), "open", Q_ARG(QVariant, true)));
    QCOMPARE(editor->find()->text(), QStringLiteral("beta"));
    QVERIFY(findItem(qobject_cast<QQuickItem *>(holder.get()), QStringLiteral("replaceField"))->isVisible());
  }
};

QTEST_MAIN(TstFind)
#include "tst_find.moc"
