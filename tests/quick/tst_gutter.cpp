#include <QtGui/QGuiApplication>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>
#include <QtQml/qqmlextensionplugin.h>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickView>
#include <QtQuick/QQuickWindow>
#include <QtQuick/qsgrendererinterface.h>
#include <QtCore/QTemporaryDir>
#include <QtTest>

#include "quick/changecolumn.h"
#include "quick/codeeditor.h"
#include "quick/delegatecolumn.h"
#include "quick/linenumbercolumn.h"
#include "quick/markercolumn.h"

Q_IMPORT_QML_PLUGIN(me_blq_qmlcodeeditorPlugin)

class TstGutter : public QObject {
  Q_OBJECT
  QQmlEngine m_engine;

  struct Shown {
    std::unique_ptr<QQuickView> view;
    CodeEditor *editor = nullptr;
  };
  Shown showEditor(int height = 300) {
    Shown shown;
    shown.view = std::make_unique<QQuickView>(&m_engine, nullptr);
    shown.view->setSource(QUrl::fromLocalFile(QFINDTESTDATA("editor.qml")));
    shown.editor = qobject_cast<CodeEditor *>(shown.view->rootObject());
    shown.editor->setWidth(400);
    shown.editor->setHeight(height);
    shown.view->show();
    if (!QTest::qWaitForWindowExposed(shown.view.get()))
      return {};
    shown.editor->forceActiveFocus();
    shown.editor->setCursorBlinkInterval(0);
    return shown;
  }

  static QString numberedLines(int count) {
    QString text;
    for (int i = 0; i < count; ++i)
      text += QStringLiteral("line %1\n").arg(i);
    return text;
  }

  // Row `row`'s vertical extent in the grabbed image, for pixel checks.
  static bool inkInGutterRow(CodeEditor *editor, const QImage &image, int row) {
    const qreal lh = editor->metrics().lineHeight();
    const QColor bg = editor->theme()->gutterBackground(), band = editor->theme()->currentLine();
    for (int y = int(row * lh); y < int((row + 1) * lh); ++y)
      for (int x = 0; x < int(editor->gutterWidth()); ++x)
        if (const QColor c = image.pixelColor(x, y); c != bg && c != band)
          return true;
    return false;
  }

private slots:
  void initTestCase() { QQuickWindow::setGraphicsApi(QSGRendererInterface::Software); }

  void noColumnsNoGutter() {
    auto [view, editor] = showEditor();
    QVERIFY(editor);
    editor->setText(QStringLiteral("abc"));
    QCOMPARE(editor->gutterWidth(), 0.0);
    QCOMPARE(editor->rectForPosition(0).left(), 0.0);
    QVERIFY(editor->gutterPlan().labels.isEmpty() && editor->gutterPlan().rects.isEmpty());
  }

  void gutterOffsetsTextAndHitTesting() {
    auto [view, editor] = showEditor();
    QVERIFY(editor);
    editor->setText(QStringLiteral("hello world\nsecond line"));
    QSignalSpy spy(editor, &CodeEditor::gutterWidthChanged);
    auto *numbers = new qce::LineNumberColumn(editor);
    editor->addGutterColumn(numbers);
    const qreal cell = editor->metrics().cellAdvance();
    QTRY_VERIFY(editor->gutterWidth() > 0);
    QCOMPARE(editor->gutterWidth(), std::ceil((2 + 2) * cell));
    QCOMPARE(spy.count(), 1);
    const qreal gw = editor->gutterWidth();
    QCOMPARE(editor->rectForPosition(0).left(), gw);
    QCOMPARE(editor->rectForPosition(3).left(), gw + 3 * cell);
    QCOMPARE(editor->positionAt(gw + 3 * cell + 1, 5), 3);
    QTest::mouseClick(view.get(), Qt::LeftButton, {}, QPoint(int(gw + 4 * cell + 1), 5), 10);
    QCOMPARE(editor->cursorPosition(), 4);
    // The text is clipped to its own area: the pixels under the gutter are the gutter's.
    editor->setCursorPosition(0);
    // Hiding the column gives the width back.
    numbers->setVisible(false);
    QTRY_COMPARE(editor->gutterWidth(), 0.0);
    QCOMPARE(editor->rectForPosition(0).left(), 0.0);
  }

  void viewportWrapUsesTheTextArea() {
    auto [view, editor] = showEditor();
    QVERIFY(editor);
    editor->setText(QString(300, u'x'));
    editor->setWrapMode(CodeEditor::WrapAtViewport);
    QTRY_COMPARE(editor->contentWidth(), editor->width());
    const qsizetype rowEndBefore = editor->displayMap().rowAt(0).endColumn;
    editor->addGutterColumn(new qce::LineNumberColumn(editor));
    QTRY_VERIFY(editor->gutterWidth() > 0);
    QCOMPARE(editor->contentWidth(), editor->width() - editor->gutterWidth());
    QTRY_VERIFY(editor->displayMap().rowAt(0).endColumn < rowEndBefore);
    QCOMPARE(editor->displayMap().wrapConfig().width, editor->width() - editor->gutterWidth());
  }

  void widthFollowsDigitCountNotScrolling() {
    auto [view, editor] = showEditor();
    QVERIFY(editor);
    editor->addGutterColumn(new qce::LineNumberColumn(editor));
    const qreal cell = editor->metrics().cellAdvance();
    editor->setText(numberedLines(98)); // 99 lines
    QTRY_COMPARE(editor->gutterWidth(), std::ceil(4 * cell));
    editor->setText(numberedLines(99)); // 100 lines
    QTRY_COMPARE(editor->gutterWidth(), std::ceil(5 * cell));
    // A long document: scrolling from top to bottom never changes the width.
    editor->setText(numberedLines(200000));
    QTRY_COMPARE(editor->gutterWidth(), std::ceil(8 * cell));
    QSignalSpy spy(editor, &CodeEditor::gutterWidthChanged);
    const qreal lh = editor->metrics().lineHeight();
    for (qreal y = 0; y < 200000 * lh; y += 4000 * lh) {
      editor->setContentY(y);
      QTest::qWait(1);
    }
    editor->setContentY(1e12);
    QTest::qWait(10);
    QCOMPARE(spy.count(), 0);
    QCOMPARE(editor->gutterWidth(), std::ceil(8 * cell));
  }

  void numbersAppearOnFirstRowsOnly_data() {
    QTest::addColumn<bool>("wrapped");
    QTest::newRow("no wrap") << false;
    QTest::newRow("wrap") << true;
  }
  void numbersAppearOnFirstRowsOnly() {
    QFETCH(bool, wrapped);
    auto [view, editor] = showEditor();
    QVERIFY(editor);
    editor->setText(QStringLiteral("hello world foo bar\nx\nyz"));
    editor->addGutterColumn(new qce::LineNumberColumn(editor));
    if (wrapped) {
      editor->setWrapMode(CodeEditor::WrapAtColumn);
      editor->setWrapColumn(10);
      QTRY_COMPARE(editor->displayMap().rowCountOfLine(0), 3);
    }
    const int line1Row = wrapped ? 3 : 1;
    QTRY_COMPARE(editor->gutterPlan().labels.size(), 3);
    const auto &labels = editor->gutterPlan().labels;
    QCOMPARE(labels[0].row, 0);
    QCOMPARE(labels[0].layout->text, QStringLiteral("1"));
    QCOMPARE(labels[1].row, line1Row);
    QCOMPARE(labels[1].layout->text, QStringLiteral("2"));
    QCOMPARE(labels[2].layout->text, QStringLiteral("3"));
    // And pixels: ink beside the first row of each line, none beside the continuation rows.
    QTRY_VERIFY(inkInGutterRow(editor, view->grabWindow(), 0));
    const QImage image = view->grabWindow();
    QVERIFY(inkInGutterRow(editor, image, line1Row));
    if (wrapped) {
      QVERIFY(!inkInGutterRow(editor, image, 1));
      QVERIFY(!inkInGutterRow(editor, image, 2));
    }
    // Numbers are right-aligned inside the column and clear of its edges.
    const qreal cell = editor->metrics().cellAdvance();
    QVERIFY(labels[0].x + labels[0].layout->width <= editor->gutterWidth() - cell + 0.01);
  }

  void relativeAndHybridModes() {
    auto [view, editor] = showEditor();
    QVERIFY(editor);
    editor->setText(numberedLines(20));
    auto *numbers = new qce::LineNumberColumn(editor);
    editor->addGutterColumn(numbers);
    editor->setCursorPosition(editor->document()->rope().lineStart(5));
    QCOMPARE(numbers->numberFor(5, 5), 6);
    QCOMPARE(numbers->numberFor(8, 5), 9);
    numbers->setMode(qce::LineNumberColumn::Relative);
    QCOMPARE(numbers->numberFor(5, 5), 0);
    QCOMPARE(numbers->numberFor(8, 5), 3);
    QCOMPARE(numbers->numberFor(2, 5), 3);
    auto textOf = [&](int row) {
      for (const auto &label : editor->gutterPlan().labels)
        if (label.row == row)
          return label.layout->text;
      return QString();
    };
    QTRY_COMPARE(textOf(5), QStringLiteral("0"));
    QCOMPARE(textOf(7), QStringLiteral("2"));
    numbers->setMode(qce::LineNumberColumn::Hybrid);
    QTRY_COMPARE(textOf(5), QStringLiteral("6"));
    QCOMPARE(textOf(7), QStringLiteral("2"));
    // Moving the cursor renumbers.
    editor->setCursorPosition(editor->document()->rope().lineStart(7));
    QTRY_COMPARE(textOf(7), QStringLiteral("8"));
    QCOMPARE(textOf(5), QStringLiteral("2"));
  }

  void currentLineNumberIsHighlighted() {
    auto [view, editor] = showEditor();
    QVERIFY(editor);
    editor->setText(numberedLines(5));
    editor->addGutterColumn(new qce::LineNumberColumn(editor));
    editor->setCursorPosition(editor->document()->rope().lineStart(2));
    QTRY_VERIFY(!editor->gutterPlan().labels.isEmpty());
    for (const auto &label : editor->gutterPlan().labels)
      QCOMPARE(label.color, label.row == 2 ? editor->theme()->currentLineNumber() : editor->theme()->lineNumber());
    // The current line's band continues across the gutter.
    const qreal lh = editor->metrics().lineHeight();
    QTRY_COMPARE(view->grabWindow().pixelColor(1, int(2.5 * lh)), editor->theme()->currentLine());
    QCOMPARE(view->grabWindow().pixelColor(1, int(1.5 * lh)), editor->theme()->gutterBackground());
  }

  void markerBarsCoverWrappedRowsAndFollowEdits() {
    auto [view, editor] = showEditor();
    QVERIFY(editor);
    editor->setText(QStringLiteral("hello world foo bar\nx\nyz"));
    editor->setWrapMode(CodeEditor::WrapAtColumn);
    editor->setWrapColumn(10);
    QTRY_COMPARE(editor->displayMap().rowCountOfLine(0), 3);
    auto *markers = new qce::MarkerColumn(editor);
    editor->addGutterColumn(markers);
    const int id = markers->addMarker(0, {{"color", QColor(Qt::red)}});
    QVERIFY(id > 0);
    auto rowsWithBar = [&] {
      QList<qsizetype> rows;
      for (const auto &rect : editor->gutterPlan().rects)
        if (rect.color == QColor(Qt::red))
          rows.append(rect.row);
      return rows;
    };
    QTRY_COMPARE(rowsWithBar(), (QList<qsizetype>{0, 1, 2}));
    const qreal lh = editor->metrics().lineHeight();
    QTRY_COMPARE(view->grabWindow().pixelColor(1, int(1.5 * lh)), QColor(Qt::red));
    QCOMPARE(view->grabWindow().pixelColor(1, int(3.5 * lh)), editor->theme()->gutterBackground());
    // A line added above pushes it down with the text.
    editor->document()->insert(0, QStringLiteral("new\n"));
    QTRY_COMPARE(rowsWithBar(), (QList<qsizetype>{1, 2, 3}));
    QCOMPARE(markers->markersAt(1), QList<int>{id});
    QVERIFY(markers->removeMarker(id));
    QTRY_VERIFY(rowsWithBar().isEmpty());
    // Highest priority wins where marks overlap.
    markers->addMarker(0, {{"color", QColor(Qt::green)}, {"priority", 1}});
    markers->addMarker(0, {{"color", QColor(Qt::blue)}, {"priority", 5}});
    QTRY_COMPARE(view->grabWindow().pixelColor(1, int(0.5 * lh)), QColor(Qt::blue));
    // Loading new text removes them.
    editor->setText(QStringLiteral("fresh"));
    QCOMPARE(markers->markerCount(), 0);
  }

  void markerIconsAreDrawnOnFirstRowsOnly() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QImage icon(8, 8, QImage::Format_ARGB32);
    icon.fill(QColor(255, 0, 255));
    const QString path = dir.filePath(QStringLiteral("icon.png"));
    QVERIFY(icon.save(path));

    auto [view, editor] = showEditor();
    QVERIFY(editor);
    editor->setText(QStringLiteral("hello world foo bar\nx"));
    editor->setWrapMode(CodeEditor::WrapAtColumn);
    editor->setWrapColumn(10);
    QTRY_COMPARE(editor->displayMap().rowCountOfLine(0), 3);
    auto *markers = new qce::MarkerColumn(editor);
    editor->addGutterColumn(markers);
    markers->addMarker(0, {{"icon", QUrl::fromLocalFile(path)}});
    QTRY_COMPARE(editor->gutterPlan().images.size(), 1);
    QCOMPARE(editor->gutterPlan().images.first().row, 0);
    const qreal lh = editor->metrics().lineHeight();
    auto magentaIn = [&](const QImage &image, int row) {
      for (int y = int(row * lh); y < int((row + 1) * lh); ++y)
        for (int x = 0; x < int(editor->gutterWidth()); ++x)
          if (image.pixelColor(x, y) == QColor(255, 0, 255))
            return true;
      return false;
    };
    QTRY_VERIFY(magentaIn(view->grabWindow(), 0));
    QVERIFY(!magentaIn(view->grabWindow(), 1));
  }

  void changeColumnMarksEditedLines() {
    auto [view, editor] = showEditor();
    QVERIFY(editor);
    editor->setText(numberedLines(10));
    auto *changes = new qce::ChangeColumn(editor);
    editor->addGutterColumn(changes);
    QVERIFY(!changes->isChanged(3));
    editor->setCursorPosition(editor->document()->rope().lineStart(3));
    QTest::keyClick(view.get(), Qt::Key_X);
    QVERIFY(changes->isChanged(3));
    QVERIFY(!changes->isChanged(2));
    QVERIFY(!changes->isChanged(4));
    const qreal lh = editor->metrics().lineHeight();
    QTRY_COMPARE(view->grabWindow().pixelColor(1, int(3.5 * lh)), editor->theme()->changeModified());
    QCOMPARE(view->grabWindow().pixelColor(1, int(2.5 * lh)), editor->theme()->gutterBackground());
    // Lines added by Enter are changed too, and the marks follow later edits.
    QTest::keyClick(view.get(), Qt::Key_Return);
    QVERIFY(changes->isChanged(4));
    editor->document()->insert(0, QStringLiteral("top\n"));
    QVERIFY(changes->isChanged(4) && changes->isChanged(5));
    QVERIFY(!changes->isChanged(3));
    // Deleting whole lines leaves a notch on the line that follows.
    const auto &rope = editor->document()->rope();
    editor->document()->remove(rope.lineStart(8), rope.lineStart(10));
    QVERIFY(changes->isChanged(8));
    // Undo is an edit like any other.
    editor->setText(numberedLines(10)); // a reset clears every mark
    QVERIFY(!changes->isChanged(3) && !changes->isChanged(4));
    // Saving clears them.
    QTest::keyClick(view.get(), Qt::Key_X);
    QVERIFY(changes->isChanged(editor->cursorLine()));
    QTemporaryDir dir;
    QSignalSpy saved(editor, &CodeEditor::saved);
    editor->save(QUrl::fromLocalFile(dir.filePath(QStringLiteral("out.txt"))));
    QTRY_COMPARE(saved.count(), 1);
    QVERIFY(!changes->isChanged(editor->cursorLine()));
  }

  void delegatesTrackThePlanAndAreReused() {
    auto [view, editor] = showEditor();
    QVERIFY(editor);
    editor->setText(numberedLines(5000));
    QQmlComponent component(&m_engine);
    component.setData(
      "import QtQuick\n"
      "Rectangle {\n"
      "  required property int line\n"
      "  required property int row\n"
      "  required property int rowInLine\n"
      "  required property bool firstRow\n"
      "  required property bool current\n"
      "  color: current ? 'yellow' : 'steelblue'\n"
      "}\n",
      QUrl());
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    auto *column = new qce::DelegateColumn(editor);
    column->setWidth(14);
    column->setDelegate(&component);
    editor->addGutterColumn(column);
    QTRY_VERIFY(column->delegateCount() > 0);
    QCOMPARE(editor->gutterWidth(), 14.0);
    QCOMPARE(column->delegateCount(), editor->renderStats().rowsInPlan);
    const int created = column->createdCount();
    QVERIFY(created <= editor->renderStats().rowsInPlan);

    const qreal lh = editor->metrics().lineHeight();
    auto check = [&] {
      const qsizetype row = qsizetype(editor->contentY() / lh) + 2;
      QQuickItem *item = column->delegateForRow(row);
      QVERIFY(item);
      QCOMPARE(item->property("line").toLongLong(), row);
      QCOMPARE(item->property("firstRow").toBool(), true);
      // Items are placed in polish, which runs before the next frame.
      QTRY_COMPARE(item->mapToItem(editor, QPointF(0, 0)).y(), qreal(row) * lh - editor->contentY());
      QCOMPARE(item->mapToItem(editor, QPointF(0, 0)).x(), 0.0);
      QCOMPARE(item->width(), 14.0);
      QCOMPARE(item->height(), lh);
    };
    check();
    // Scroll a long way in small steps and in jumps: delegates are recycled, not created.
    for (int i = 0; i < 300; ++i) {
      editor->setContentY(editor->contentY() + 0.37 * lh * (1 + i % 5));
      QTest::qWait(1);
    }
    check();
    editor->setContentY(3000 * lh + 3);
    QTRY_VERIFY(column->delegateForRow(3002));
    check();
    editor->setContentY(0);
    QTRY_VERIFY(column->delegateForRow(2));
    QCOMPARE(column->delegateCount(), editor->renderStats().rowsInPlan);
    QVERIFY2(column->createdCount() <= created + 2 * editor->renderStats().rowsInPlan,
             qPrintable(QString::number(column->createdCount())));
    // The cursor line's delegate is told.
    editor->setCursorPosition(editor->document()->rope().lineStart(2));
    QTRY_VERIFY(column->delegateForRow(2)->property("current").toBool());
    QVERIFY(!column->delegateForRow(3)->property("current").toBool());
  }

  void delegatesSkipContinuationRowsByDefault() {
    auto [view, editor] = showEditor();
    QVERIFY(editor);
    editor->setText(QStringLiteral("hello world foo bar\nx"));
    editor->setWrapMode(CodeEditor::WrapAtColumn);
    editor->setWrapColumn(10);
    QTRY_COMPARE(editor->displayMap().rowCountOfLine(0), 3);
    QQmlComponent component(&m_engine);
    component.setData(
      "import QtQuick\nItem { required property int line; required property int row;"
      " required property int rowInLine; required property bool firstRow; required property bool current }",
      QUrl());
    QVERIFY(component.isReady());
    auto *column = new qce::DelegateColumn(editor);
    column->setDelegate(&component);
    editor->addGutterColumn(column);
    QTRY_COMPARE(column->delegateCount(), 2);
    QVERIFY(column->delegateForRow(0) && column->delegateForRow(3));
    QVERIFY(!column->delegateForRow(1));
    column->setFirstRowsOnly(false);
    QTRY_COMPARE(column->delegateCount(), 4);
    QCOMPARE(column->delegateForRow(1)->property("rowInLine").toInt(), 1);
    QVERIFY(!column->delegateForRow(1)->property("firstRow").toBool());
  }

  void clickingTheGutterSelectsLines() {
    auto [view, editor] = showEditor();
    QVERIFY(editor);
    editor->setText(QStringLiteral("first\nsecond\nthird\nfourth"));
    auto *numbers = new qce::LineNumberColumn(editor);
    editor->addGutterColumn(numbers);
    QTRY_VERIFY(editor->gutterWidth() > 0);
    QSignalSpy clicked(numbers, &qce::GutterColumn::clicked);
    const qreal lh = editor->metrics().lineHeight();
    auto at = [&](int row) { return QPoint(int(editor->gutterWidth() / 2), int((row + 0.5) * lh)); };
    const auto &rope = editor->document()->rope();

    QTest::mouseClick(view.get(), Qt::LeftButton, {}, at(1), 10);
    QCOMPARE(clicked.count(), 1);
    QCOMPARE(clicked.first().at(0).toLongLong(), 1);
    QCOMPARE(clicked.first().at(1).toInt(), int(Qt::LeftButton));
    QCOMPARE(editor->selectionStart(), rope.lineStart(1));
    QCOMPARE(editor->selectionEnd(), rope.lineStart(2));

    // Dragging down grows the selection by whole lines, up past the first line grows it the other way.
    QTest::mousePress(view.get(), Qt::LeftButton, {}, at(1), 10);
    QTest::mouseMove(view.get(), at(3));
    QTest::mouseRelease(view.get(), Qt::LeftButton, {}, at(3), 10);
    QCOMPARE(editor->selectionStart(), rope.lineStart(1));
    QCOMPARE(editor->selectionEnd(), rope.length());
    QTest::mousePress(view.get(), Qt::LeftButton, {}, at(2), 10);
    QTest::mouseMove(view.get(), at(0));
    QTest::mouseRelease(view.get(), Qt::LeftButton, {}, at(0), 10);
    QCOMPARE(editor->selectionStart(), 0);
    QCOMPARE(editor->selectionEnd(), rope.lineStart(3));

    // Shift extends from where the selection began.
    QTest::mouseClick(view.get(), Qt::LeftButton, {}, at(0), 10);
    QTest::mouseClick(view.get(), Qt::LeftButton, Qt::ShiftModifier, at(2), 10);
    QCOMPARE(editor->selectionStart(), 0);
    QCOMPARE(editor->selectionEnd(), rope.lineStart(3));
    QCOMPARE(clicked.count(), 6);

    // The pointer is a plain arrow over the gutter and a text cursor over the text.
    QCOMPARE(editor->cursor().shape(), Qt::ArrowCursor);
    QTest::mouseMove(view.get(), QPoint(int(editor->gutterWidth()) + 20, 10));
    QCOMPARE(editor->cursor().shape(), Qt::IBeamCursor);
  }

  void clickingAWrappedLineSelectsTheWholeLine() {
    auto [view, editor] = showEditor();
    QVERIFY(editor);
    editor->setText(QStringLiteral("hello world foo bar\nx"));
    editor->setWrapMode(CodeEditor::WrapAtColumn);
    editor->setWrapColumn(10);
    QTRY_COMPARE(editor->displayMap().rowCountOfLine(0), 3);
    editor->addGutterColumn(new qce::LineNumberColumn(editor));
    QTRY_VERIFY(editor->gutterWidth() > 0);
    const qreal lh = editor->metrics().lineHeight();
    // Row 2 is a continuation row of line 0.
    QTest::mouseClick(view.get(), Qt::LeftButton, {}, QPoint(2, int(2.5 * lh)), 10);
    QCOMPARE(editor->selectionStart(), 0);
    QCOMPARE(editor->selectionEnd(), 20);
  }

  void markerClickReportsTheLine() {
    auto [view, editor] = showEditor();
    QVERIFY(editor);
    editor->setText(numberedLines(10));
    auto *markers = new qce::MarkerColumn(editor);
    editor->addGutterColumn(markers);
    QSignalSpy clicked(markers, &qce::GutterColumn::clicked);
    const qreal lh = editor->metrics().lineHeight();
    QTRY_VERIFY(editor->gutterWidth() > 0);
    QTest::mouseClick(view.get(), Qt::LeftButton, {}, QPoint(3, int(4.5 * lh)), 10);
    QCOMPARE(clicked.count(), 1);
    QCOMPARE(clicked.first().at(0).toLongLong(), 4);
    // A column that does not select lines leaves the selection alone.
    QCOMPARE(editor->selectionStart(), editor->selectionEnd());
  }

  void nodesAreReusedWhileScrollingWithAGutter() {
    auto [view, editor] = showEditor();
    QVERIFY(editor);
    editor->setText(numberedLines(20000));
    editor->addGutterColumn(new qce::LineNumberColumn(editor));
    editor->addGutterColumn(new qce::ChangeColumn(editor));
    const qreal lh = editor->metrics().lineHeight();
    for (int i = 0; i < 50; ++i) {
      editor->setContentY(i * 3.3 * lh);
      view->grabWindow();
    }
    const auto before = editor->renderStats().scene;
    for (int i = 50; i < 400; ++i) {
      editor->setContentY(i * 3.3 * lh);
      view->grabWindow();
    }
    const auto after = editor->renderStats().scene;
    QCOMPARE(after.nodesCreated, before.nodesCreated);
    QVERIFY(after.nodesRecycled > before.nodesRecycled);
  }
};

QTEST_MAIN(TstGutter)
#include "tst_gutter.moc"
