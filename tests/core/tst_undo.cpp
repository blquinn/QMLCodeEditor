#include "core/textdocument.h"
#include "core/undostack.h"
#include "testutil.h"

#include <QtTest>

using namespace qce;
using namespace Qt::StringLiterals;

namespace {

SelectionList sel(qsizetype a, qsizetype h) { return {Selection{a, h}}; }

EditRecord typed(qsizetype at, const QString &text) { return {at, Rope(), Rope::fromString(text)}; }

} // namespace

class TstUndo : public QObject {
  Q_OBJECT
private slots:
  // ---- UndoStack coalescing rules, with a fake clock ----

  void typingCoalescesWhenContiguousAndQuick() {
    qint64 now = 0;
    UndoStack s([&] { return now; });
    s.pushEdit(typed(0, u"a"_s), EditKind::Typing, sel(0, 0), sel(1, 1));
    now = 200;
    s.pushEdit(typed(1, u"b"_s), EditKind::Typing, sel(1, 1), sel(2, 2));
    now = 400;
    s.pushEdit(typed(2, u"c"_s), EditKind::Typing, sel(2, 2), sel(3, 3));
    QCOMPARE(s.undoSteps(), 1);
    const Transaction t = s.undo();
    // A run of typing is one record: memory per keystroke is the text itself (PERF-03).
    QCOMPARE(t.edits.size(), 1);
    QCOMPARE(t.edits.first().start, 0);
    QCOMPARE(t.edits.first().inserted.toString(), u"abc"_s);
    QVERIFY(t.edits.first().removed.isEmpty());
    QCOMPARE(t.selectionsBefore, sel(0, 0));
    QCOMPARE(t.selectionsAfter, sel(3, 3));
  }

  void deletingRunsMergeIntoOneRecord() {
    qint64 now = 0;
    UndoStack back([&] { return now; });
    auto removed = [](qsizetype at, const QString &text) { return EditRecord{at, Rope::fromString(text), Rope()}; };
    // Backspacing "cde" from the end of "abcde": the deleted text is "cde", starting at 2.
    back.pushEdit(removed(4, u"e"_s), EditKind::DeleteBackward, sel(5, 5), sel(4, 4));
    back.pushEdit(removed(3, u"d"_s), EditKind::DeleteBackward, sel(4, 4), sel(3, 3));
    back.pushEdit(removed(2, u"c"_s), EditKind::DeleteBackward, sel(3, 3), sel(2, 2));
    QCOMPARE(back.undoSteps(), 1);
    Transaction t = back.undo();
    QCOMPARE(t.edits.size(), 1);
    QCOMPARE(t.edits.first().start, 2);
    QCOMPARE(t.edits.first().removed.toString(), u"cde"_s);
    // Delete key: each key removes the next character at the same place.
    UndoStack forward([&] { return now; });
    forward.pushEdit(removed(2, u"c"_s), EditKind::DeleteForward, sel(2, 2), sel(2, 2));
    forward.pushEdit(removed(2, u"d"_s), EditKind::DeleteForward, sel(2, 2), sel(2, 2));
    forward.pushEdit(removed(2, u"e"_s), EditKind::DeleteForward, sel(2, 2), sel(2, 2));
    QCOMPARE(forward.undoSteps(), 1);
    t = forward.undo();
    QCOMPARE(t.edits.size(), 1);
    QCOMPARE(t.edits.first().start, 2);
    QCOMPARE(t.edits.first().removed.toString(), u"cde"_s);
  }

  void typingSplitsOnTimeout() {
    qint64 now = 0;
    UndoStack s([&] { return now; });
    s.pushEdit(typed(0, u"a"_s), EditKind::Typing, {}, {});
    now = 1500;
    s.pushEdit(typed(1, u"b"_s), EditKind::Typing, {}, {});
    QCOMPARE(s.undoSteps(), 2);
  }

  void typingSplitsWhenNotContiguous() {
    UndoStack s([] { return qint64(0); });
    s.pushEdit(typed(0, u"a"_s), EditKind::Typing, {}, {});
    s.pushEdit(typed(5, u"b"_s), EditKind::Typing, {}, {});
    QCOMPARE(s.undoSteps(), 2);
  }

  void lineBreakEndsAStep() {
    UndoStack s([] { return qint64(0); });
    s.pushEdit(typed(0, u"a"_s), EditKind::Typing, {}, {});
    s.pushEdit(typed(1, u"\n"_s), EditKind::Typing, {}, {});
    s.pushEdit(typed(2, u"b"_s), EditKind::Typing, {}, {});
    QCOMPARE(s.undoSteps(), 3);
  }

  void differentKindsDoNotMerge() {
    UndoStack s([] { return qint64(0); });
    s.pushEdit(typed(0, u"a"_s), EditKind::Typing, {}, {});
    s.pushEdit({1, Rope(), Rope::fromString(u"b")}, EditKind::Other, {}, {});
    QCOMPARE(s.undoSteps(), 2);
  }

  void deletesCoalesceInTheirDirection() {
    UndoStack s([] { return qint64(0); });
    auto del = [](qsizetype at, const QString &t) { return EditRecord{at, Rope::fromString(t), Rope()}; };
    s.pushEdit(del(9, u"x"_s), EditKind::DeleteBackward, {}, {});
    s.pushEdit(del(8, u"y"_s), EditKind::DeleteBackward, {}, {});
    s.pushEdit(del(7, u"z"_s), EditKind::DeleteBackward, {}, {});
    QCOMPARE(s.undoSteps(), 1);
    s.clear();
    s.pushEdit(del(3, u"x"_s), EditKind::DeleteForward, {}, {});
    s.pushEdit(del(3, u"y"_s), EditKind::DeleteForward, {}, {});
    QCOMPARE(s.undoSteps(), 1);
    s.pushEdit(del(0, u"q"_s), EditKind::DeleteForward, {}, {});
    QCOMPARE(s.undoSteps(), 2);
  }

  void breakCoalescingForcesNewStep() {
    UndoStack s([] { return qint64(0); });
    s.pushEdit(typed(0, u"a"_s), EditKind::Typing, {}, {});
    s.breakCoalescing();
    s.pushEdit(typed(1, u"b"_s), EditKind::Typing, {}, {});
    QCOMPARE(s.undoSteps(), 2);
  }

  void newEditClearsRedoAndLimitDropsOldest() {
    UndoStack s([] { return qint64(0); });
    s.setLimit(2);
    for (int i = 0; i < 4; ++i)
      s.pushEdit(typed(i * 10, u"x"_s), EditKind::Other, {}, {});
    QCOMPARE(s.undoSteps(), 2);
    s.undo();
    QVERIFY(s.canRedo());
    s.pushEdit(typed(0, u"y"_s), EditKind::Other, {}, {});
    QVERIFY(!s.canRedo());
  }

  // ---- through TextDocument ----

  void groupsUndoAsOneStep() {
    TextDocument doc;
    doc.setText(u"hello");
    doc.beginEditGroup(sel(0, 0));
    doc.insert(5, u" world");
    doc.beginEditGroup(sel(99, 99)); // nested: ignored
    doc.replace(0, 1, u"J");
    doc.endEditGroup(sel(98, 98));
    doc.endEditGroup(sel(11, 11));
    QCOMPARE(doc.rope().toString(), QStringLiteral("Jello world"));
    QCOMPARE(doc.undoStack().undoSteps(), 1);

    auto restored = doc.undo();
    QVERIFY(restored);
    QCOMPARE(*restored, sel(0, 0));
    QCOMPARE(doc.rope().toString(), QStringLiteral("hello"));
    restored = doc.redo();
    QCOMPARE(*restored, sel(11, 11));
    QCOMPARE(doc.rope().toString(), QStringLiteral("Jello world"));
    QVERIFY(!doc.redo());
  }

  void typingRunsUndoTogetherAndRestoreSelections() {
    TextDocument doc;
    doc.undoStack().setClock([] { return qint64(0); });
    doc.setText(u"");
    for (int i = 0; i < 5; ++i)
      doc.insert(i, u"x", {EditKind::Typing, sel(i, i), sel(i + 1, i + 1)});
    QCOMPARE(doc.undoStack().undoSteps(), 1);
    const auto r = doc.undo();
    QCOMPARE(*r, sel(0, 0));
    QVERIFY(doc.rope().isEmpty());
    QCOMPARE(*doc.redo(), sel(5, 5));
    QCOMPARE(doc.rope().toString(), QStringLiteral("xxxxx"));
  }

  void undoRefusedWhileGroupOpenAndEmptyGroupsIgnored() {
    TextDocument doc;
    doc.insert(0, u"a");
    doc.beginEditGroup();
    QVERIFY(!doc.canUndo());
    QVERIFY(!doc.undo());
    doc.endEditGroup();
    doc.beginEditGroup();
    doc.endEditGroup();
    QCOMPARE(doc.undoStack().undoSteps(), 1);
  }

  void undoEmitsChangesAndMovesAnchors() {
    TextDocument doc;
    doc.setText(u"0123456789");
    const AnchorId a = doc.anchors().create(8, Gravity::Left);
    doc.insert(2, u"abc");
    QCOMPARE(doc.anchors().offset(a), 11);
    QList<TextChange> changes;
    connect(&doc, &TextDocument::changed, this, [&](const TextChange &c) { changes << c; });
    doc.undo();
    QCOMPARE(changes.size(), 1);
    QCOMPARE(changes[0].removed.toString(), QStringLiteral("abc"));
    QCOMPARE(doc.anchors().offset(a), 8);
  }

  void setTextClearsHistory() {
    TextDocument doc;
    doc.insert(0, u"a");
    QVERIFY(doc.canUndo());
    doc.setText(u"fresh");
    QVERIFY(!doc.canUndo());
  }

  // Random grouped edit sequences: undo-all and redo-all must pass through every recorded state.
  void randomSequencesRoundTrip() {
    test::Random rnd(test::testSeed());
    for (int round = 0; round < test::testIterations(30); ++round) {
      TextDocument doc;
      doc.setText(rnd.text(rnd.range(0, 3000)));
      QStringList states{doc.rope().toString()};
      QList<SelectionList> befores, afters;
      const int steps = rnd.range(1, 40);
      for (int s = 0; s < steps; ++s) {
        const SelectionList before = sel(rnd.below(100), rnd.below(100));
        const SelectionList after = sel(rnd.below(100), rnd.below(100));
        doc.beginEditGroup(before);
        const int edits = rnd.range(1, 4);
        for (int e = 0; e < edits; ++e) {
          const qsizetype at = rnd.below(int(doc.length()) + 1);
          doc.replace(at, at + rnd.below(60), rnd.text(rnd.below(60)));
        }
        doc.endEditGroup(after);
        if (doc.rope().toString() == states.last() && doc.undoStack().undoSteps() < states.size())
          continue; // all edits were no-ops, so no step was recorded
        states << doc.rope().toString();
        befores << before;
        afters << after;
        QCOMPARE(doc.undoStack().undoSteps(), int(states.size()) - 1);
      }
      for (qsizetype i = states.size() - 1; i > 0; --i) {
        const auto r = doc.undo();
        QVERIFY(r);
        QCOMPARE(*r, befores[i - 1]);
        QVERIFY(doc.rope().toString() == states[i - 1]);
      }
      QVERIFY(!doc.canUndo());
      for (qsizetype i = 1; i < states.size(); ++i) {
        const auto r = doc.redo();
        QVERIFY(r);
        QCOMPARE(*r, afters[i - 1]);
        QVERIFY(doc.rope().toString() == states[i]);
      }
      QVERIFY(!doc.canRedo());
      QVERIFY(doc.anchors().validate());
    }
  }
};

QTEST_APPLESS_MAIN(TstUndo)
#include "tst_undo.moc"
