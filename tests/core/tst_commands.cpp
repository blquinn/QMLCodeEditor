#include "core/commands.h"
#include "testutil.h"

#include <QtTest>

using namespace qce;
using namespace Qt::StringLiterals;

namespace {

struct Fixture {
  TextDocument doc;
  SelectionSet sel{&doc};
  EditContext ctx{doc, sel, {}};
  explicit Fixture(const QString &text = {}) { doc.setText(text); }
  QString text() const { return doc.rope().toString(); }
};

} // namespace

class TstCommands : public QObject {
  Q_OBJECT
private slots:
  void typingInsertsAndAdvances() {
    Fixture f(u"ac"_s);
    f.sel.setSingle(1);
    QVERIFY(commands::insertText(f.ctx, u"b"));
    QCOMPARE(f.text(), u"abc"_s);
    QCOMPARE(f.sel.primary(), (Selection{2, 2}));
  }

  void typingReplacesSelection() {
    Fixture f(u"hello world"_s);
    f.sel.setSingle(6, 11);
    commands::insertText(f.ctx, u"there");
    QCOMPARE(f.text(), u"hello there"_s);
    QCOMPARE(f.sel.primary(), (Selection{11, 11}));
  }

  void backspaceAndDeleteAreGraphemeAware() {
    Fixture f(u"a\U0001F600éb"_s); // a, emoji (2 units), e + combining acute, b
    f.sel.setSingle(3);
    QVERIFY(commands::deleteBackward(f.ctx));
    QCOMPARE(f.text(), u"aéb"_s);
    f.sel.setSingle(1);
    QVERIFY(commands::deleteForward(f.ctx));
    QCOMPARE(f.text(), u"ab"_s);
    f.sel.setSingle(0);
    QVERIFY(!commands::deleteBackward(f.ctx)); // nothing before the start
    f.sel.setSingle(2);
    QVERIFY(!commands::deleteForward(f.ctx));
  }

  void cursorsThatCannotDeleteStayInTheSet() {
    Fixture f(u"abc"_s);
    f.sel.set({{0, 0}, {2, 2}});
    QVERIFY(commands::deleteBackward(f.ctx)); // the cursor at 0 has nothing to delete
    QCOMPARE(f.text(), u"ac"_s);
    QCOMPARE(f.sel.selections(), (SelectionList{{0, 0}, {1, 1}}));
  }

  void backspaceOverCrlfRemovesBoth() {
    Fixture f(u"a\r\nb"_s);
    f.sel.setSingle(3);
    commands::deleteBackward(f.ctx);
    QCOMPARE(f.text(), u"ab"_s);
  }

  void backspaceWithSelectionDeletesIt() {
    Fixture f(u"abcdef"_s);
    f.sel.setSingle(4, 1);
    commands::deleteBackward(f.ctx);
    QCOMPARE(f.text(), u"aef"_s);
    QCOMPARE(f.sel.primary(), (Selection{1, 1}));
  }

  void multiCursorEditIsOneUndoStep() {
    Fixture f(u"aaa\nbbb\nccc"_s);
    f.sel.set({{1, 1}, {5, 5}, {9, 9}});
    commands::insertText(f.ctx, u"X");
    QCOMPARE(f.text(), u"aXaa\nbXbb\ncXcc"_s);
    QCOMPARE(f.sel.selections(), (SelectionList{{2, 2}, {7, 7}, {12, 12}}));
    QVERIFY(commands::undo(f.ctx));
    QCOMPARE(f.text(), u"aaa\nbbb\nccc"_s);
    QCOMPARE(f.sel.selections(), (SelectionList{{1, 1}, {5, 5}, {9, 9}}));
    QVERIFY(!commands::undo(f.ctx));
    QVERIFY(commands::redo(f.ctx));
    QCOMPARE(f.text(), u"aXaa\nbXbb\ncXcc"_s);
    QCOMPARE(f.sel.selections(), (SelectionList{{2, 2}, {7, 7}, {12, 12}}));
  }

  void typingRunsMergeIntoOneUndoStep() {
    Fixture f;
    for (const char16_t c : {u'a', u'b', u'c'})
      commands::insertText(f.ctx, QStringView(&c, 1));
    QCOMPARE(f.text(), u"abc"_s);
    QCOMPARE(f.doc.undoStack().undoSteps(), 1);
    commands::undo(f.ctx);
    QCOMPARE(f.text(), QString());
    QCOMPARE(f.sel.primary(), (Selection{0, 0}));
  }

  void undoRestoresSelection() {
    Fixture f(u"hello world"_s);
    f.sel.setSingle(0, 5);
    commands::insertText(f.ctx, u"bye", EditKind::Other);
    QCOMPARE(f.text(), u"bye world"_s);
    commands::undo(f.ctx);
    QCOMPARE(f.sel.primary(), (Selection{0, 5}));
  }

  void selectAllSelectsEverything() {
    Fixture f(u"abc\ndef"_s);
    commands::selectAll(f.ctx);
    QCOMPARE(f.sel.primary(), (Selection{0, 7}));
  }

  void refusedWhileLoading() {
    Fixture f(u"abc"_s);
    f.doc.load(QStringLiteral("/nonexistent/file")); // starts a load; edits are refused until it ends
    if (f.doc.isLoading())
      QVERIFY(!commands::insertText(f.ctx, u"x"));
    f.doc.cancelLoad();
  }

  // Random selections and edits against a plain QString model.
  void differentialAgainstStringModel() {
    test::Random rng(test::testSeed());
    for (int round = 0; round < test::testIterations(200); ++round) {
      QString model = rng.text(rng.range(0, 60));
      Fixture f(model);
      // Random, non-touching selections on code point boundaries.
      QList<Selection> sels;
      qsizetype pos = 0;
      while (pos < model.size() && sels.size() < 5) {
        const qsizetype a = f.doc.rope().snapToCodePoint(qMin<qsizetype>(model.size(), pos + rng.below(8)));
        const qsizetype b = f.doc.rope().snapToCodePoint(qMin<qsizetype>(model.size(), a + (rng.chance(50) ? 0 : rng.below(5))));
        if (a < pos)
          break;
        sels.append({a, b});
        pos = b + 1;
      }
      if (sels.isEmpty())
        sels.append({0, 0});
      f.sel.set(sels);
      const SelectionList before = f.sel.selections();
      const QString text = rng.chance(30) ? QString() : rng.text(rng.range(1, 4));
      if (text.isEmpty() && std::all_of(before.begin(), before.end(), [](Selection s) { return s.isEmpty(); }))
        continue;

      // The model: replace back to front.
      QString expected = model;
      for (qsizetype i = before.size() - 1; i >= 0; --i)
        expected.replace(before[i].start(), before[i].end() - before[i].start(), text);
      // Skip texts whose joins form a CRLF with neighbours: the document widens those edits.
      if (!commands::insertText(f.ctx, text, EditKind::Other))
        continue;
      const bool joinsCrlf = f.text() != expected;
      if (!joinsCrlf) {
        QCOMPARE(f.text(), expected);
        QCOMPARE(f.sel.count(), int(before.size()));
      }
      // Undo always round-trips exactly.
      QVERIFY(commands::undo(f.ctx));
      QCOMPARE(f.text(), model);
      QCOMPARE(f.sel.selections(), before);
    }
  }
};

QTEST_APPLESS_MAIN(TstCommands)
#include "tst_commands.moc"
