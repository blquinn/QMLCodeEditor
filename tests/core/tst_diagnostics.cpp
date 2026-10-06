#include "core/diagnostics.h"
#include "core/textdocument.h"

#include <QtTest>

using namespace qce;

namespace {

QString numberedLines(int count) {
  QString text;
  for (int i = 0; i < count; ++i)
    text += QStringLiteral("line %1\n").arg(i); // 7 units for N < 10
  return text;
}

Diagnostic diag(qsizetype line, qsizetype from, qsizetype to, int severity = ErrorSeverity, const QString &message = {}) {
  Diagnostic d;
  d.start = {line, from};
  d.end = {line, to};
  d.severity = severity;
  d.message = message;
  return d;
}

QVariantMap lspPosition(int line, int character) { return {{"line", line}, {"character", character}}; }

} // namespace

class TstDiagnostics : public QObject {
  Q_OBJECT
private slots:
  void lspJsonRoundTrips() {
    const QVariantMap json{
      {"range", QVariantMap{{"start", lspPosition(2, 4)}, {"end", lspPosition(2, 9)}}},
      {"severity", 2},
      {"message", "unused variable"},
      {"code", "no-unused"},
      {"source", "eslint"},
      {"relatedInformation",
       QVariantList{QVariantMap{
         {"location", QVariantMap{{"uri", "file:///a.js"},
                                  {"range", QVariantMap{{"start", lspPosition(0, 1)}, {"end", lspPosition(0, 3)}}}}},
         {"message", "declared here"}}}},
      {"tags", QVariantList{1, 2}},
      {"data", QVariantMap{{"fix", 7}}}};
    const Diagnostic d = Diagnostic::fromLsp(json);
    QCOMPARE(d.start, (TextPosition{2, 4}));
    QCOMPARE(d.end, (TextPosition{2, 9}));
    QCOMPARE(d.severity, WarningSeverity);
    QCOMPARE(d.message, QStringLiteral("unused variable"));
    QCOMPARE(d.code.toString(), QStringLiteral("no-unused"));
    QCOMPARE(d.source, QStringLiteral("eslint"));
    QCOMPARE(d.related.size(), 1);
    QCOMPARE(d.related[0].uri, QStringLiteral("file:///a.js"));
    QCOMPARE(d.related[0].end, (TextPosition{0, 3}));
    QCOMPARE(d.related[0].message, QStringLiteral("declared here"));
    QCOMPARE(d.tags, (QList<int>{UnnecessaryTag, DeprecatedTag}));
    QCOMPARE(d.data.toMap().value("fix").toInt(), 7);
    QCOMPARE(Diagnostic::fromLsp(d.toLsp()).toLsp(), d.toLsp());
    QCOMPARE(d.toLsp().value("range").toMap().value("end").toMap().value("character").toInt(), 9);
  }

  void missingFieldsGetDefaults() {
    const Diagnostic d = Diagnostic::fromLsp({{"message", "x"}});
    QCOMPARE(d.severity, ErrorSeverity);
    QCOMPARE(d.start, (TextPosition{0, 0}));
    QVERIFY(!d.code.isValid());
    const QVariantMap back = d.toLsp();
    QVERIFY(!back.contains("code"));
    QVERIFY(!back.contains("source"));
    QVERIFY(!back.contains("tags"));
    QCOMPARE(Diagnostic::fromLsp({{"severity", 9}}).severity, ErrorSeverity); // out of range
  }

  void becomesDecorationsInItsOwnLayer() {
    TextDocument doc;
    doc.setText(numberedLines(10));
    DecorationSet decorations(&doc);
    DiagnosticSet set(&doc, &decorations);
    QSignalSpy spy(&set, &DiagnosticSet::changed);
    set.setDiagnostics({diag(1, 0, 4, ErrorSeverity), diag(2, 0, 4, HintSeverity), diag(3, 0, 4, InfoSeverity)});
    QCOMPARE(spy.size(), 1);
    QCOMPARE(set.count(), 3);
    QCOMPARE(decorations.count(DecorationKind::Squiggle), 2); // hints are underlines
    QCOMPARE(decorations.count(DecorationKind::Underline), 1);
    QCOMPARE(decorations.count(DecorationKind::GutterIcon), 3);
    QCOMPARE(decorations.count(DecorationKind::EndOfLineText), 0);
    QCOMPARE(decorations.layerSize(DiagnosticSet::kLayer), 6);
    set.setEndOfLineMessages(true);
    QCOMPARE(decorations.count(DecorationKind::EndOfLineText), 3);
    set.setEndOfLineMessages(false);
    QCOMPARE(decorations.count(DecorationKind::EndOfLineText), 0);
    // A host's own layer is untouched.
    DecorationSpec spec;
    spec.start = 0;
    spec.end = 3;
    decorations.add(spec, 0);
    set.setDiagnostics({diag(0, 0, 1)});
    QCOMPARE(decorations.layerSize(0), 1);
    QCOMPARE(decorations.layerSize(DiagnosticSet::kLayer), 2);
    set.clear();
    QCOMPARE(set.count(), 0);
    QCOMPARE(decorations.layerSize(DiagnosticSet::kLayer), 0);
    QCOMPARE(decorations.size(), 1);
  }

  void positionsAreClampedToTheText() {
    TextDocument doc;
    doc.setText(QStringLiteral("abc\ndef"));
    DecorationSet decorations(&doc);
    DiagnosticSet set(&doc, &decorations);
    set.setDiagnostics({diag(1, 1, 99), diag(50, 0, 3), diag(0, 3, 1)}); // past the line, past the text, reversed
    const QList<Diagnostic> all = set.inRange(0, doc.length());
    QCOMPARE(all.size(), 3);
    for (const Diagnostic &d : all)
      QVERIFY(d.start <= d.end);
    QCOMPARE(set.at(5).first().end, (TextPosition{1, 3})); // clamped to the end of "def"
  }

  void atFindsTheCharacterUnderTheOffsetMostSevereFirst() {
    TextDocument doc;
    doc.setText(numberedLines(5));
    DecorationSet decorations(&doc);
    DiagnosticSet set(&doc, &decorations);
    set.setDiagnostics(
      {diag(0, 0, 6, WarningSeverity, "warn"), diag(0, 2, 4, ErrorSeverity, "err"), diag(0, 5, 5, InfoSeverity, "empty")}
    );
    QCOMPARE(set.at(0).size(), 1);
    const auto both = set.at(3);
    QCOMPARE(both.size(), 2);
    QCOMPARE(both[0].message, QStringLiteral("err"));
    QCOMPARE(both[1].message, QStringLiteral("warn"));
    QCOMPARE(set.at(4).size(), 1); // the error ends at 4: its last character is at 3
    QCOMPARE(set.at(6).size(), 0); // the warning's last character is at 5
    QCOMPARE(set.at(5).size(), 2); // warning and the empty range sitting at 5
  }

  void rangesFollowEdits() {
    TextDocument doc;
    doc.setText(numberedLines(5));
    DecorationSet decorations(&doc);
    DiagnosticSet set(&doc, &decorations);
    set.setDiagnostics({diag(2, 2, 6, ErrorSeverity, "e")});
    doc.insert(0, u"new\n");
    const auto moved = set.inRange(0, doc.length());
    QCOMPARE(moved.size(), 1);
    QCOMPARE(moved[0].start, (TextPosition{3, 2}));
    QCOMPARE(moved[0].end, (TextPosition{3, 6}));
    QCOMPARE(moved[0].message, QStringLiteral("e"));
    doc.insert(doc.rope().lineStart(3) + 3, u"XY"); // inside: grows
    QCOMPARE(set.inRange(0, doc.length())[0].end, (TextPosition{3, 8}));
    QCOMPARE(set.at(doc.rope().lineStart(3) + 3).first().toLsp().value("range").toMap().value("end").toMap().value("character").toInt(), 8);
  }

  void resetDropsTheList() {
    TextDocument doc;
    doc.setText(numberedLines(3));
    DecorationSet decorations(&doc);
    DiagnosticSet set(&doc, &decorations);
    set.setDiagnostics({diag(1, 0, 2)});
    QSignalSpy spy(&set, &DiagnosticSet::changed);
    doc.setText(QStringLiteral("other"));
    QCOMPARE(set.count(), 0);
    QCOMPARE(spy.size(), 1);
    QCOMPARE(decorations.size(), 0);
    QCOMPARE(doc.anchors().size(), 0);
  }

  void replacingTheListLeaksNoAnchors() {
    TextDocument doc;
    doc.setText(numberedLines(100));
    DecorationSet decorations(&doc);
    DiagnosticSet set(&doc, &decorations);
    for (int round = 0; round < 5; ++round) {
      QList<Diagnostic> list;
      for (int i = 0; i < 50; ++i)
        list.append(diag(i, 0, 3));
      set.setDiagnostics(list);
    }
    QCOMPARE(doc.anchors().size(), 50 * 2 * 2); // two decorations of two anchors per diagnostic
    set.clear();
    QCOMPARE(doc.anchors().size(), 0);
  }

  void nextAndPreviousWalkInStartOrder() {
    TextDocument doc;
    doc.setText(numberedLines(10));
    DecorationSet decorations(&doc);
    DiagnosticSet set(&doc, &decorations);
    set.setDiagnostics({diag(5, 1, 3, WarningSeverity), diag(2, 0, 3, ErrorSeverity), diag(8, 0, 3, HintSeverity)});
    const auto &rope = doc.rope();
    QCOMPARE(set.next(0)->start.line, 2);
    QCOMPARE(set.next(rope.lineStart(2))->start.line, 5);
    QCOMPARE(set.next(rope.lineStart(5) + 1)->start.line, 8);
    QCOMPARE(set.next(rope.lineStart(8))->start.line, 2); // wraps
    QVERIFY(!set.next(rope.lineStart(8), false));
    QCOMPARE(set.previous(rope.lineStart(5) + 1)->start.line, 2);
    QCOMPARE(set.previous(rope.lineStart(2))->start.line, 8); // wraps
    QVERIFY(!set.previous(rope.lineStart(2), false));
    // Only the severe ones.
    QCOMPARE(set.next(0, true, WarningSeverity)->start.line, 2);
    QCOMPARE(set.next(rope.lineStart(5) + 1, true, WarningSeverity)->start.line, 2);
    QVERIFY(!set.next(rope.lineStart(5) + 1, false, WarningSeverity));
    DiagnosticSet empty(&doc, &decorations);
    QVERIFY(!empty.next(0));
  }

  void ahundredThousandAreOneCall() {
    TextDocument doc;
    doc.setText(numberedLines(100000));
    DecorationSet decorations(&doc);
    DiagnosticSet set(&doc, &decorations);
    QList<Diagnostic> list;
    list.reserve(100000);
    for (int i = 0; i < 100000; ++i)
      list.append(diag(i, 0, 4, 1 + i % 4, QStringLiteral("m")));
    QElapsedTimer timer;
    timer.start();
    set.setDiagnostics(list);
    qInfo() << "100k diagnostics set in" << timer.elapsed() << "ms";
    QCOMPARE(set.count(), 100000);
    QVERIFY(decorations.validate());
    QCOMPARE(set.at(doc.rope().lineStart(77777) + 1).size(), 1);
    QCOMPARE(set.inRange(doc.rope().lineStart(500), doc.rope().lineEnd(509)).size(), 10);
  }
};

QTEST_GUILESS_MAIN(TstDiagnostics)
#include "tst_diagnostics.moc"
