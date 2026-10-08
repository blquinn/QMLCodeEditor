#include "core/findreplace.h"

#include <QtTest>

using namespace qce;
using namespace Qt::StringLiterals;

namespace {

struct Fixture {
  TextDocument doc;
  SelectionSet sel{&doc};
  EditorSettings settings;
  FindReplace find{doc, sel, [this] { return EditContext{doc, sel, settings}; }};
  explicit Fixture(const QString &text) { doc.setText(text); }
  QString text() const { return doc.rope().toString(); }
  // Types a query and waits for its search.
  void search(const QString &query, int expected) {
    find.setText(query);
    find.setActive(true);
    QTRY_COMPARE(find.matchCount(), expected);
    QTRY_VERIFY(!find.busy());
  }
};

} // namespace

class TstFindReplace : public QObject {
  Q_OBJECT
private slots:
  void countsAndSelectsFirstFromCursor() {
    Fixture f(u"foo bar foo\nfoo"_s);
    f.sel.setSingle(5);
    f.search(u"foo"_s, 3);
    QCOMPARE(f.sel.primary(), (Selection{8, 11}));
    QCOMPARE(f.find.currentIndex(), 1);
    QVERIFY(!f.find.capped());
  }

  void incrementalSearchStartsFromTheOriginalCursor() {
    Fixture f(u"abc abd abe"_s);
    f.sel.setSingle(4);
    f.find.setActive(true);
    f.find.setText(u"a"_s);
    QTRY_COMPARE(f.sel.primary(), (Selection{4, 5}));
    f.find.setText(u"ab"_s);
    QTRY_COMPARE(f.sel.primary(), (Selection{4, 6})); // still the match at the cursor, not the next one
    QCOMPARE(f.find.matchCount(), 3);
    f.find.setText(u"abe"_s);
    QTRY_COMPARE(f.sel.primary(), (Selection{8, 11}));
    QCOMPARE(f.find.matchCount(), 1);
  }

  void nextAndPreviousWrap() {
    Fixture f(u"x a x a x"_s);
    f.search(u"x"_s, 3);
    QCOMPARE(f.sel.primary(), (Selection{0, 1}));
    QVERIFY(f.find.next());
    QCOMPARE(f.sel.primary(), (Selection{4, 5}));
    QVERIFY(f.find.next());
    QVERIFY(f.find.next());
    QCOMPARE(f.sel.primary(), (Selection{0, 1})); // wrapped
    QVERIFY(f.find.previous());
    QCOMPARE(f.sel.primary(), (Selection{8, 9}));
    QCOMPARE(f.find.currentIndex(), 2);
  }

  void nextWorksBeforeTheSearchHasFinished() {
    Fixture f(u"a b a"_s);
    f.find.setText(u"a"_s);
    QVERIFY(f.find.next());
    QCOMPARE(f.sel.primary(), (Selection{0, 1}));
    QVERIFY(f.find.next());
    QCOMPARE(f.sel.primary(), (Selection{4, 5}));
  }

  void options() {
    Fixture f(u"Foo foo foobar"_s);
    f.search(u"foo"_s, 3);
    f.find.setCaseSensitive(true);
    QTRY_COMPARE(f.find.matchCount(), 2);
    f.find.setCaseSensitive(false);
    f.find.setWholeWord(true);
    QTRY_COMPARE(f.find.matchCount(), 2);
    f.find.setWholeWord(false);
    f.find.setRegex(true);
    f.find.setText(u"fo+"_s);
    QTRY_COMPARE(f.find.matchCount(), 3);
    f.find.setText(u"o(?=b)"_s);
    QTRY_COMPARE(f.find.matchCount(), 1);
  }

  void invalidExpressionReportsAnError() {
    Fixture f(u"(a)"_s);
    f.find.setRegex(true);
    f.search(u"x"_s, 0);
    f.find.setText(u"("_s);
    QVERIFY(!f.find.error().isEmpty());
    QCOMPARE(f.find.matchCount(), 0);
    QVERIFY(!f.find.next());
    QVERIFY(f.find.highlightPattern().pattern().isEmpty());
    f.find.setText(u"\\("_s);
    QVERIFY(f.find.error().isEmpty());
    QTRY_COMPARE(f.find.matchCount(), 1);
  }

  void highlightFollowsActive() {
    Fixture f(u"a"_s);
    QSignalSpy spy(&f.find, &FindReplace::highlightChanged);
    f.find.setText(u"a"_s);
    QVERIFY(f.find.highlightPattern().pattern().isEmpty()); // not active yet
    f.find.setActive(true);
    QVERIFY(!f.find.highlightPattern().pattern().isEmpty());
    f.find.setActive(false);
    QVERIFY(f.find.highlightPattern().pattern().isEmpty());
    QVERIFY(spy.count() >= 3);
  }

  void editsRefreshTheCount() {
    Fixture f(u"foo foo"_s);
    f.search(u"foo"_s, 2);
    f.doc.insert(0, u"foo ");
    QTRY_COMPARE(f.find.matchCount(), 3);
    f.doc.remove(0, 8);
    QTRY_COMPARE(f.find.matchCount(), 1);
  }

  void replaceMovesOneMatchAtATime() {
    Fixture f(u"cat dog cat"_s);
    f.search(u"cat"_s, 2);
    f.find.setReplacement(u"bird"_s);
    QVERIFY(f.find.replace());
    QCOMPARE(f.text(), u"bird dog cat"_s);
    QCOMPARE(f.sel.primary(), (Selection{9, 12})); // on the next match
    QVERIFY(f.find.replace());
    QCOMPARE(f.text(), u"bird dog bird"_s);
    QTRY_COMPARE(f.find.matchCount(), 0);
  }

  void replaceAllIsOneUndoStep() {
    Fixture f(u"a a a\na"_s);
    f.search(u"a"_s, 4);
    f.find.setReplacement(u"bb"_s);
    f.find.replaceAll();
    QVERIFY(f.find.busy() || f.text() == u"bb bb bb\nbb"_s);
    QTRY_COMPARE(f.text(), u"bb bb bb\nbb"_s);
    QTRY_VERIFY(!f.find.busy());
    QCOMPARE(f.sel.primary(), (Selection{11, 11}));
    f.sel.setSingle(0);
    QVERIFY(f.doc.undo().has_value());
    QCOMPARE(f.text(), u"a a a\na"_s);
    QVERIFY(!f.doc.canUndo());
  }

  void regexReplacementExpandsGroups() {
    Fixture f(u"ann@x bob@y"_s);
    f.find.setRegex(true);
    f.find.setReplacement(u"$2:$1 \\t$$ $&"_s);
    f.search(u"(\\w+)@(\\w+)"_s, 2);
    f.find.replaceAll();
    QTRY_COMPARE(f.text(), u"x:ann \t$ ann@x y:bob \t$ bob@y"_s);
  }

  void plainReplacementIsLiteral() {
    Fixture f(u"a a"_s);
    f.find.setReplacement(u"$1\\n"_s);
    f.search(u"a"_s, 2);
    f.find.replaceAll();
    QTRY_COMPARE(f.text(), u"$1\\n $1\\n"_s);
  }

  void emptyMatchesAreReplacedInRegexMode() {
    Fixture f(u"a\nb"_s);
    f.find.setRegex(true);
    f.find.setReplacement(u"// "_s);
    f.search(u"^"_s, 0);
    f.find.replaceAll();
    QTRY_COMPARE(f.text(), u"// a\n// b"_s);
  }

  void replaceAllRunsAgainWhenTheTextMovedOn() {
    Fixture f(u"a a"_s);
    f.search(u"a"_s, 2);
    f.find.setReplacement(u"b"_s);
    f.find.replaceAll();
    f.doc.insert(0, u"a "); // before the worker's edits can land
    QTRY_VERIFY(!f.find.busy());
    QTRY_COMPARE(f.text(), u"b b b"_s);
  }

  void readOnlyRefusesToReplace() {
    Fixture f(u"a a"_s);
    f.search(u"a"_s, 2);
    f.settings.readOnly = true;
    f.find.setReplacement(u"b"_s);
    f.find.replaceAll();
    QVERIFY(!f.find.replace());
    QTest::qWait(50);
    QCOMPARE(f.text(), u"a a"_s);
  }

  void selectAllMatches() {
    Fixture f(u"x y x y x"_s);
    f.sel.setSingle(3);
    f.search(u"x"_s, 3);
    QVERIFY(f.find.selectAllMatches());
    QCOMPARE(f.sel.count(), 3);
    QCOMPARE(f.sel.primary(), (Selection{4, 5})); // the one nearest the old cursor, at or after it
  }

  void cappedListsStillNavigate() {
    QString text;
    for (int i = 0; i < FindReplace::kMaxMatches + 5; ++i)
      text += u"x\n"_s;
    Fixture f(text);
    f.find.setText(u"x"_s);
    f.find.setActive(true);
    QTRY_VERIFY(f.find.capped());
    QCOMPARE(f.find.matchCount(), int(FindReplace::kMaxMatches));
    f.sel.setSingle(2 * (FindReplace::kMaxMatches + 1) + 1);
    QVERIFY(f.find.next()); // past the end of the list: found in the text
    QCOMPARE(f.sel.primary().start(), qsizetype(2 * (FindReplace::kMaxMatches + 2)));
  }

  void useSelectionSeedsTheText() {
    Fixture f(u"hello world"_s);
    f.sel.setSingle(2, 2);
    f.find.useSelection();
    QCOMPARE(f.find.text(), u"hello"_s);
    f.sel.setSingle(6, 11);
    f.find.useSelection();
    QCOMPARE(f.find.text(), u"world"_s);
    f.find.setRegex(true);
    f.sel.setSingle(0, 0);
    f.sel.setSingle(0, 5);
    f.find.useSelection();
    QCOMPARE(f.find.text(), u"hello"_s);
  }
};

QTEST_GUILESS_MAIN(TstFindReplace)
#include "tst_findreplace.moc"
