#include "quick/textmetrics.h"

#include <QtGui/QFontDatabase>
#include <QtGui/QGuiApplication>
#include <QtGui/QTextLayout>
#include <QtGui/QTextOption>
#include <QtTest>

#include <random>

using qce::TextMetrics;

class TstTextMetrics : public QObject {
  Q_OBJECT

  static std::unique_ptr<QTextLayout> layoutFor(const TextMetrics &m, const QString &text) {
    auto layout = std::make_unique<QTextLayout>(text, m.layoutFont());
    QTextOption option;
    option.setTabStopDistance(m.tabWidth() * m.cellAdvance());
    layout->setTextOption(option);
    layout->beginLayout();
    QTextLine line = layout->createLine();
    line.setLineWidth(1e6);
    layout->endLayout();
    return layout;
  }

private slots:
  void initTestCase() {
    m_metrics = TextMetrics(TextMetrics::defaultMonospaceFont(), 4);
    if (!m_metrics.isMonospace())
      QSKIP("no monospace font available");
  }

  void basicMetrics() {
    QVERIFY(m_metrics.lineHeight() >= m_metrics.ascent());
    QVERIFY(m_metrics.cellAdvance() > 0);
  }

  void simpleClassification() {
    QVERIFY(m_metrics.isSimple(u"int main() {\t}"));
    QVERIFY(m_metrics.isSimple(u""));
    QVERIFY(!m_metrics.isSimple(u"café"));
    QVERIFY(!m_metrics.isSimple(u"中文"));
    QVERIFY(!m_metrics.isSimple(u"a\U0001F600"));
    QVERIFY(!m_metrics.isSimple(u"é"));
    TextMetrics proportional(QFont(QStringLiteral("sans-serif")));
    if (!proportional.isMonospace())
      QVERIFY(!proportional.isSimple(u"abc"));
  }

  void fastPathMatchesLayout() {
    std::mt19937 rng(7);
    const QString alphabet = QStringLiteral("abcXYZ019 (){};,.-><=\t\t");
    for (int n = 0; n < 200; ++n) {
      QString text;
      const int len = rng() % 60;
      for (int i = 0; i < len; ++i)
        text += alphabet[rng() % alphabet.size()];
      QVERIFY(m_metrics.isSimple(text));
      const auto layout = layoutFor(m_metrics, text);
      const QTextLine line = layout->lineAt(0);
      for (int col = 0; col <= text.size(); ++col) {
        QVERIFY2(
          qAbs(line.cursorToX(col) - m_metrics.xForColumn(text, col)) < 0.01,
          qPrintable(QStringLiteral("%1 col %2").arg(text).arg(col))
        );
      }
    }
  }

  void columnForXRoundTrips() {
    const QString text = QStringLiteral("a\tbc\t\td");
    for (int col = 0; col <= text.size(); ++col)
      QCOMPARE(m_metrics.columnForX(text, m_metrics.xForColumn(text, col)), col);
    QCOMPARE(m_metrics.columnForX(text, -5), 0);
    QCOMPARE(m_metrics.columnForX(text, 1e6), text.size());
    // Halfway into a tab's span rounds to the nearer edge.
    const qreal tabStart = m_metrics.xForColumn(text, 1), tabEnd = m_metrics.xForColumn(text, 2);
    QCOMPARE(m_metrics.columnForX(text, tabStart + (tabEnd - tabStart) * 0.4), 1);
    QCOMPARE(m_metrics.columnForX(text, tabStart + (tabEnd - tabStart) * 0.6), 2);
  }

  void tabWidthChangesStops() {
    TextMetrics m = m_metrics;
    m.setTabWidth(8);
    QCOMPARE(m.cellForColumn(u"a\tb", 2), 8);
    m.setTabWidth(2);
    QCOMPARE(m.cellForColumn(u"a\tb", 2), 2);
  }

private:
  TextMetrics m_metrics;
};

QTEST_MAIN(TstTextMetrics)
#include "tst_textmetrics.moc"
