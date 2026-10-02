#include "core/textdocument.h"
#include "testutil.h"

#include <QtTest>

using namespace qce;

class TstTextDocument : public QObject {
  Q_OBJECT
private slots:
  void insertEmitsChange() {
    TextDocument doc;
    doc.setText(u"hello\nworld");
    QList<TextChange> changes;
    connect(&doc, &TextDocument::changed, this, [&](const TextChange &c) { changes << c; });

    const quint64 v0 = doc.version();
    QVERIFY(doc.insert(8, u"XY\nZ"));
    QCOMPARE(doc.rope().toString(), QStringLiteral("hello\nwoXY\nZrld"));
    QCOMPARE(changes.size(), 1);
    const TextChange &c = changes[0];
    QCOMPARE(c.start, 8);
    QCOMPARE(c.oldEnd, 8);
    QCOMPARE(c.newEnd, 12);
    QCOMPARE(c.startPos, (TextPosition{1, 2}));
    QCOMPARE(c.oldEndPos, (TextPosition{1, 2}));
    QCOMPARE(c.newEndPos, (TextPosition{2, 1}));
    QCOMPARE(c.inserted.toString(), QStringLiteral("XY\nZ"));
    QVERIFY(c.removed.isEmpty());
    QCOMPARE(c.versionBefore, v0);
    QCOMPARE(c.versionAfter, v0 + 1);
    QCOMPARE(doc.version(), v0 + 1);
  }

  void removeAndReplace() {
    TextDocument doc;
    doc.setText(u"ab\ncd\nef");
    QList<TextChange> changes;
    connect(&doc, &TextDocument::changed, this, [&](const TextChange &c) { changes << c; });
    doc.replace(1, 7, u"-");
    QCOMPARE(doc.rope().toString(), QStringLiteral("a-f"));
    QCOMPARE(changes[0].removed.toString(), QStringLiteral("b\ncd\ne"));
    QCOMPARE(changes[0].oldEndPos, (TextPosition{2, 1})); // on the text before the edit
    QCOMPARE(changes[0].newEndPos, (TextPosition{0, 2})); // on the text after it
    doc.remove(0, 99);
    QVERIFY(doc.rope().isEmpty());
    QCOMPARE(changes[1].oldEnd, 3);
  }

  void noOpEditDoesNotEmit() {
    TextDocument doc;
    doc.setText(u"abc");
    int n = 0;
    connect(&doc, &TextDocument::changed, this, [&](const TextChange &) { ++n; });
    const quint64 v = doc.version();
    QVERIFY(doc.insert(1, u""));
    QVERIFY(doc.remove(2, 2));
    QCOMPARE(n, 0);
    QCOMPARE(doc.version(), v);
  }

  void editsSnapToCodePoints() {
    TextDocument doc;
    doc.setText(u"a\U0001F600b");
    QVERIFY(doc.insert(2, u"X")); // inside the pair: lands before it
    QCOMPARE(doc.rope().toString(), QStringLiteral("aX\U0001F600b"));
    doc.setText(u"a\U0001F600b");
    QVERIFY(doc.remove(2, 3)); // second half only: removes the whole pair
    QCOMPARE(doc.rope().toString(), QStringLiteral("ab"));
  }

  void editsDoNotSplitCrlf() {
    TextDocument doc;
    doc.setText(u"a\r\nb");
    QVERIFY(doc.insert(2, u"X")); // between CR and LF
    QCOMPARE(doc.rope().toString(), QStringLiteral("aX\r\nb"));
    QVERIFY(doc.remove(3, 4)); // the LF alone: removes the whole break
    QCOMPARE(doc.rope().toString(), QStringLiteral("aXb"));
  }

  void editsThatFormACrlfAreWidened() {
    TextDocument doc;
    doc.setText(u"a\nb");
    QList<TextChange> changes;
    connect(&doc, &TextDocument::changed, this, [&](const TextChange &c) { changes << c; });
    QVERIFY(doc.insert(1, u"x\r")); // new text would end between CR and LF
    QCOMPARE(doc.rope().toString(), QStringLiteral("ax\r\nb"));
    QCOMPARE(changes[0].inserted.toString(), QStringLiteral("x\r\n"));
    QCOMPARE(changes[0].removed.toString(), QStringLiteral("\n"));
    QCOMPARE(changes[0].newEndPos, (TextPosition{1, 0}));
  }

  void resetEmitsTextReset() {
    TextDocument doc;
    QSignalSpy spy(&doc, &TextDocument::textReset);
    doc.setText(u"x");
    QCOMPARE(spy.count(), 1);
  }

  void changesReplayOntoACopy() {
    // Applying each TextChange to a QString copy reproduces the document: the contract that
    // consumers like tree-sitter and LSP clients rely on.
    test::Random rnd(test::testSeed());
    TextDocument doc;
    doc.setText(rnd.text(20'000));
    QString mirror = doc.rope().toString();
    connect(&doc, &TextDocument::changed, this, [&](const TextChange &c) {
      QCOMPARE(mirror.mid(c.start, c.removedLength()), c.removed.toString());
      mirror.replace(c.start, c.removedLength(), c.inserted.toString());
      Rope m = Rope::fromString(mirror);
      QCOMPARE(m.offsetAt(c.startPos), c.start);
      QCOMPARE(m.offsetAt(c.newEndPos), c.newEnd);
    });
    for (int i = 0; i < test::testIterations(300); ++i) {
      const qsizetype a = rnd.below(int(doc.length()) + 1);
      doc.replace(a, a + rnd.below(200), rnd.text(rnd.below(200)));
    }
    QCOMPARE(doc.rope().toString(), mirror);
  }
};

QTEST_APPLESS_MAIN(TstTextDocument)
#include "tst_textdocument.moc"
