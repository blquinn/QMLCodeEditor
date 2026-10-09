// Host token styles and highlight overlays (API-13, API-14): an overlay's spans reach the screen in the
// theme's color for its style, and an overlay that names lines re-lays out only those.
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlEngine>
#include <QtQml/qqmlextensionplugin.h>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickView>
#include <QtQuick/QQuickWindow>
#include <QtQuick/qsgrendererinterface.h>
#include <QtTest>

#include "core/highlighter.h"
#include "quick/codeeditor.h"
#include "quick/theme.h"

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

// An antialiased glyph in `color` over the dark theme: mostly that color.
bool hasInk(const QImage &image, const QRect &rect, const QColor &color) {
  for (int y = rect.top(); y < rect.bottom() && y < image.height(); ++y)
    for (int x = rect.left(); x < rect.right() && x < image.width(); ++x) {
      const QColor c = image.pixelColor(x, y);
      if (qAbs(c.red() - color.red()) < 40 && qAbs(c.green() - color.green()) < 40 &&
          qAbs(c.blue() - color.blue()) < 40)
        return true;
    }
  return false;
}

// Spans given per line; counts how often each line is asked for (the editor asks once per layout it builds).
class FakeOverlay : public qce::Highlighter {
public:
  QHash<qsizetype, QList<qce::HighlightSpan>> spans;
  QHash<qsizetype, int> asked;
  int attached = 0, detached = 0;

  void attach(qce::TextDocument *) override { ++attached; }
  void detach() override { ++detached; }
  QList<QList<qce::HighlightSpan>> highlightLines(const qce::TextSnapshot &text, qsizetype first, qsizetype last) override {
    QList<QList<qce::HighlightSpan>> out;
    for (qsizetype line = qMax<qsizetype>(first, 0); line <= qMin(last, text.lineCount() - 1); ++line) {
      ++asked[line];
      out.append(spans.value(line));
    }
    return out;
  }
};

} // namespace

class TstOverlays : public QObject {
  Q_OBJECT
  QQmlEngine m_engine;

  struct Shown {
    std::unique_ptr<QQuickView> view;
    CodeEditor *editor = nullptr;
  };
  Shown showEditor() {
    Shown shown;
    shown.view = std::make_unique<QQuickView>(&m_engine, nullptr);
    shown.view->setSource(QUrl::fromLocalFile(QFINDTESTDATA("editor.qml")));
    shown.editor = qobject_cast<CodeEditor *>(shown.view->rootObject());
    shown.editor->setWidth(400);
    shown.editor->setHeight(200);
    shown.view->show();
    if (!QTest::qWaitForWindowExposed(shown.view.get()))
      return {};
    shown.editor->setCursorBlinkInterval(0);
    return shown;
  }
  static QRect rowRect(CodeEditor *editor, int row) {
    const int lh = int(editor->metrics().lineHeight());
    return QRect(int(editor->gutterWidth()), row * lh, 400, lh);
  }

private slots:
  void initTestCase() { QQuickWindow::setGraphicsApi(QSGRendererInterface::Software); }

  void overlayStyleColorsItsGlyphsOnly() {
    auto [view, editor] = showEditor();
    editor->setText(QStringLiteral("alpha\nbeta\ngamma"));
    const qce::TokenStyle style = qce::registerTokenStyle(u"overlay.red");
    editor->theme()->setTokenStyle(QStringLiteral("overlay.red"), kRed);
    FakeOverlay overlay;
    overlay.spans[1] = {{0, 4, style}};
    editor->addOverlay(&overlay);
    QTRY_VERIFY(hasInk(view->grabWindow(), rowRect(editor, 1), kRed));
    const QImage image = view->grabWindow();
    QVERIFY(!hasInk(image, rowRect(editor, 0), kRed));
    QVERIFY(!hasInk(image, rowRect(editor, 2), kRed));
    // Beyond the styled columns the row keeps the theme's text color.
    const int right = int(editor->gutterWidth() + 4 * editor->metrics().cellAdvance()) + 2;
    QVERIFY(!hasInk(image, QRect(right, rowRect(editor, 1).top(), 100, rowRect(editor, 1).height()), kRed));
    editor->removeOverlay(&overlay);
  }

  void themeStyleBackgroundIsDrawnBehindTheRange() {
    auto [view, editor] = showEditor();
    editor->setText(QStringLiteral("alpha\nbeta"));
    const qce::TokenStyle style = qce::registerTokenStyle(u"overlay.tint");
    editor->theme()->setTokenStyle(QStringLiteral("overlay.tint"), kGreen, kBlue);
    FakeOverlay overlay;
    overlay.spans[0] = {{1, 3, style}};
    editor->addOverlay(&overlay);
    QTRY_VERIFY(hasColor(view->grabWindow(), rowRect(editor, 0), kBlue));
    QImage image = view->grabWindow();
    QVERIFY(!hasColor(image, rowRect(editor, 1), kBlue));
    // "lph" only: the band starts after the first cell and ends before the fifth.
    const qreal cell = editor->metrics().cellAdvance();
    const int top = rowRect(editor, 0).top() + 2;
    QCOMPARE(image.pixelColor(int(editor->gutterWidth() + cell * 0.5), top), image.pixelColor(2, top)); // not the band
    QCOMPARE(image.pixelColor(int(editor->gutterWidth() + cell * 2.5), top), kBlue);
    QVERIFY(image.pixelColor(int(editor->gutterWidth() + cell * 4.5), top) != kBlue);

    // A style without a background draws none.
    editor->theme()->setTokenStyle(QStringLiteral("overlay.tint"), kGreen);
    QTRY_VERIFY(!hasColor(view->grabWindow(), rowRect(editor, 0), kBlue));
    QVERIFY(hasInk(view->grabWindow(), rowRect(editor, 0), kGreen));
    editor->removeOverlay(&overlay);
  }

  void settingTheStyleAgainRestylesTheText() {
    auto [view, editor] = showEditor();
    editor->setText(QStringLiteral("alpha"));
    const qce::TokenStyle style = qce::registerTokenStyle(u"overlay.restyle");
    editor->theme()->setTokenStyle(QStringLiteral("overlay.restyle"), kRed);
    FakeOverlay overlay;
    overlay.spans[0] = {{0, 5, style}};
    editor->addOverlay(&overlay);
    QTRY_VERIFY(hasInk(view->grabWindow(), rowRect(editor, 0), kRed));
    editor->theme()->setTokenStyle(QStringLiteral("overlay.restyle"), kGreen);
    QTRY_VERIFY(hasInk(view->grabWindow(), rowRect(editor, 0), kGreen));
    QVERIFY(!hasInk(view->grabWindow(), rowRect(editor, 0), kRed));
    editor->removeOverlay(&overlay);
  }

  void aStyleRegisteredAfterTheThemeWasBuiltStillColors() {
    auto [view, editor] = showEditor();
    editor->setText(QStringLiteral("alpha"));
    // The theme learns the color first; the style is registered by the time a layout asks for it.
    editor->theme()->setTokenStyles({{QStringLiteral("overlay.late"), QVariantMap{{QStringLiteral("color"), kRed}}}});
    const qce::TokenStyle style = qce::registerTokenStyle(u"overlay.late");
    FakeOverlay overlay;
    overlay.spans[0] = {{0, 5, style}};
    editor->addOverlay(&overlay);
    QTRY_VERIFY(hasInk(view->grabWindow(), rowRect(editor, 0), kRed));
    editor->removeOverlay(&overlay);
  }

  void laterOverlaysWin() {
    auto [view, editor] = showEditor();
    editor->setText(QStringLiteral("alpha"));
    const qce::TokenStyle red = qce::registerTokenStyle(u"overlay.first");
    const qce::TokenStyle green = qce::registerTokenStyle(u"overlay.second");
    editor->theme()->setTokenStyle(QStringLiteral("overlay.first"), kRed);
    editor->theme()->setTokenStyle(QStringLiteral("overlay.second"), kGreen);
    FakeOverlay first, second;
    first.spans[0] = {{0, 5, red}};
    second.spans[0] = {{2, 1, green}};
    editor->addOverlay(&first);
    editor->addOverlay(&second);
    QCOMPARE(editor->overlayList(), (QList<qce::Highlighter *>{&first, &second}));
    QTRY_VERIFY(hasInk(view->grabWindow(), rowRect(editor, 0), kGreen));
    QVERIFY(hasInk(view->grabWindow(), rowRect(editor, 0), kRed));
    // Taking the later one away leaves the earlier one everywhere.
    editor->removeOverlay(&second);
    QTRY_VERIFY(!hasInk(view->grabWindow(), rowRect(editor, 0), kGreen));
    editor->removeOverlay(&first);
    QTRY_VERIFY(!hasInk(view->grabWindow(), rowRect(editor, 0), kRed));
  }

  void anOverlayAboveTheMainHighlighterKeepsItsOtherSpans() {
    auto [view, editor] = showEditor();
    editor->setText(QStringLiteral("alpha beta"));
    FakeOverlay base, overlay;
    base.spans[0] = {{0, 5, qce::TokenStyle::Keyword}, {6, 4, qce::TokenStyle::Keyword}};
    const qce::TokenStyle style = qce::registerTokenStyle(u"overlay.mid");
    editor->theme()->setTokenStyle(QStringLiteral("overlay.mid"), kRed);
    overlay.spans[0] = {{3, 5, style}}; // "ha be" cuts both keywords
    editor->setHighlighter(&base);
    editor->addOverlay(&overlay);
    const QColor keyword(0x56, 0x9c, 0xd6);
    QTRY_VERIFY(hasInk(view->grabWindow(), rowRect(editor, 0), kRed));
    QVERIFY(hasInk(view->grabWindow(), rowRect(editor, 0), keyword));
    editor->removeOverlay(&overlay);
    editor->setHighlighter(nullptr);
  }

  void invalidatingOneLineRelaysOutOnlyThatLine() {
    auto [view, editor] = showEditor();
    editor->setText(QStringLiteral("a0\na1\na2\na3\na4"));
    FakeOverlay overlay;
    editor->addOverlay(&overlay);
    QTRY_VERIFY(overlay.asked.value(4) > 0); // every line has been laid out
    QTest::qWait(50);
    overlay.asked.clear();
    emit overlay.invalidated(2, 2);
    QTRY_COMPARE(overlay.asked.value(2), 1);
    QTest::qWait(50);
    QCOMPARE(overlay.asked.size(), 1); // no other line was asked for
    QCOMPARE(overlay.asked.value(2), 1);

    overlay.asked.clear();
    emit overlay.invalidated(1, 3);
    QTRY_VERIFY(overlay.asked.value(3) > 0);
    QVERIFY(!overlay.asked.contains(0) && !overlay.asked.contains(4));

    overlay.asked.clear();
    emit overlay.invalidated(qce::Highlighter::AllLines, qce::Highlighter::AllLines);
    QTRY_VERIFY(overlay.asked.value(0) > 0 && overlay.asked.value(4) > 0);
    editor->removeOverlay(&overlay);
  }

  void overlaysAreAttachedAndDetached() {
    auto [view, editor] = showEditor();
    FakeOverlay overlay;
    editor->addOverlay(&overlay);
    editor->addOverlay(&overlay); // already there
    QCOMPARE(overlay.attached, 1);
    QCOMPARE(editor->overlayList().size(), 1);
    editor->removeOverlay(&overlay);
    QCOMPARE(overlay.detached, 1);
    QVERIFY(editor->overlayList().isEmpty());
    editor->removeOverlay(&overlay); // not there: nothing happens
    QCOMPARE(overlay.detached, 1);
  }

  void aDestroyedOverlayLeavesTheList() {
    auto [view, editor] = showEditor();
    editor->setText(QStringLiteral("alpha"));
    auto *overlay = new FakeOverlay;
    editor->addOverlay(overlay);
    QCOMPARE(editor->overlayList().size(), 1);
    delete overlay;
    QVERIFY(editor->overlayList().isEmpty());
  }

  void editsKeepOverlaySpansOnTheirTextWhenTheOverlayInvalidates() {
    auto [view, editor] = showEditor();
    editor->setText(QStringLiteral("alpha\nbeta"));
    const qce::TokenStyle style = qce::registerTokenStyle(u"overlay.edit");
    editor->theme()->setTokenStyle(QStringLiteral("overlay.edit"), kRed);
    FakeOverlay overlay;
    overlay.spans[1] = {{0, 4, style}};
    editor->addOverlay(&overlay);
    QTRY_VERIFY(hasInk(view->grabWindow(), rowRect(editor, 1), kRed));
    // The host re-scans after an edit and names what moved; the overlay is the one that knows where its spans are.
    editor->document()->insert(0, QStringLiteral("new\n"));
    overlay.spans.clear();
    overlay.spans[2] = {{0, 4, style}};
    emit overlay.invalidated(qce::Highlighter::AllLines, qce::Highlighter::AllLines);
    QTRY_VERIFY(hasInk(view->grabWindow(), rowRect(editor, 2), kRed));
    QVERIFY(!hasInk(view->grabWindow(), rowRect(editor, 1), kRed));
    editor->removeOverlay(&overlay);
  }

  // A line millions of pixels wide: the selection far to the right must start and end on the pixels of
  // its columns (PERF-01; a float scene would be off by whole pixels out there).
  void aSelectionFarToTheRightIsDrawnOnItsColumns() {
    auto [view, editor] = showEditor();
    QVERIFY(editor);
    if (!editor->metrics().isMonospace())
      QSKIP("needs a monospace font");
    editor->forceActiveFocus();
    constexpr qsizetype n = 6'000'000;
    editor->setText(QString(n, u'x'));
    QTRY_COMPARE(editor->document()->length(), n);
    QTRY_VERIFY(editor->contentWidth() > 1'000'000);
    const qreal cell = editor->metrics().cellAdvance();
    editor->setContentX((n - 120) * cell - 100);
    QTRY_VERIFY(editor->renderStats().polishCalls > 0);
    QTest::qWait(100);
    editor->select(n - 120, n - 100);
    // The software renderer works out what to repaint for a new node before the scroll transform is
    // applied, so a selection that first appears in a scrolled view stays unpainted until something moves.
    QTest::qWait(100);
    editor->setContentX(editor->contentX() + 1);
    const QColor selection = editor->theme()->selection();
    QRectF from, to;
    QImage image;
    QTRY_VERIFY2_WITH_TIMEOUT(
      [&] {
        from = editor->rectForPosition(n - 120);
        to = editor->rectForPosition(n - 100);
        image = view->grabWindow();
        return from.left() > 0 && to.left() < image.width() && image.pixelColor(int(from.left()) + 3, 3) == selection;
      }(),
      qPrintable(QStringLiteral("selection not on screen: from %1 to %2 contentX %3").arg(from.left()).arg(to.left()).arg(editor->contentX())),
      5000
    );
    const int y = 3;
    // Inside the selection and just outside it, at the pixel.
    QCOMPARE(image.pixelColor(int(from.left()), y), selection);
    QCOMPARE(image.pixelColor(int(to.left()) - 1, y), selection);
    QVERIFY(image.pixelColor(int(from.left()) - 1, y) != selection);
    QVERIFY(image.pixelColor(int(to.left()), y) != selection);
  }
};

QTEST_MAIN(TstOverlays)
#include "tst_overlays.moc"
