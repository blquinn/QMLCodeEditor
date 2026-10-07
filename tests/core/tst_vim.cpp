// Scripted vim tests: initial text and cursor, keystrokes in vim notation, expected text and cursor.
//
// Markup: '|' is an empty cursor, '‹' and '›' are the start and end of a selection. Several cursors
// are several '|'. The expected string is compared after the keys ran, with the same markup.
#include "core/commands.h"
#include "core/cursorlayout.h"
#include "core/vim/vimhandler.h"
#include "core/vim/vimkeys.h"
#include "testutil.h"

#include <QtTest>

#include <random>

using namespace qce;
using namespace Qt::StringLiterals;

namespace {

struct Host : InputHost {
  QString clipboard, selectionClipboard;
  qsizetype scrolled = 0;
  QList<FoldCommand> folds;
  QList<bool> diagnostics;
  QRegularExpression highlight;
  InputHost::VisibleRows rows;
  void copy() override {}
  void cut() override {}
  void paste() override {}
  void scrollRows(qsizetype n) override { scrolled += n; }
  void foldCommand(FoldCommand c) override { folds.append(c); }
  void gotoDiagnostic(bool forward) override { diagnostics.append(forward); }
  VisibleRows visibleRows() const override { return rows; }
  QString clipboardText(bool selection) override { return selection ? selectionClipboard : clipboard; }
  void setClipboardText(const QString &text, bool selection) override { (selection ? selectionClipboard : clipboard) = text; }
  void setSearchHighlight(const QRegularExpression &re) override { highlight = re; }
};

struct Marked {
  QString text;
  SelectionList selections;
};

Marked parseMarked(const QString &marked) {
  Marked out;
  qsizetype start = -1;
  for (QChar c : marked) {
    if (c == u'|') {
      out.selections.append({out.text.size(), out.text.size()});
    } else if (c == u'‹') {
      start = out.text.size();
    } else if (c == u'›') {
      out.selections.append({start, out.text.size()});
      start = -1;
    } else {
      out.text += c;
    }
  }
  if (out.selections.isEmpty())
    out.selections.append({0, 0});
  return out;
}

QString render(const QString &text, const SelectionList &selections) {
  struct Mark {
    qsizetype at;
    QChar mark;
  };
  QList<Mark> marks;
  for (const Selection &s : selections) {
    if (s.isEmpty()) {
      marks.append({s.head, u'|'});
    } else {
      marks.append({s.start(), u'‹'});
      marks.append({s.end(), u'›'});
    }
  }
  std::stable_sort(marks.begin(), marks.end(), [](const Mark &a, const Mark &b) { return a.at < b.at; });
  QString out;
  qsizetype at = 0;
  for (const Mark &m : marks) {
    out += text.mid(at, m.at - at);
    out += m.mark;
    at = m.at;
  }
  out += text.mid(at);
  return out;
}

struct Fixture {
  TextDocument doc;
  SelectionSet sel{&doc};
  DisplayMap map{&doc};
  GridCursorLayout layout{&doc, 4, 5, &map};
  EditContext ctx{doc, sel, {}, &map, &layout};
  VimInputHandler vim;
  Host host;

  explicit Fixture(const QString &marked) {
    const Marked m = parseMarked(marked);
    doc.setText(m.text);
    sel.set(m.selections, 0);
    vim.activate(ctx, host);
  }
  bool keys(const QString &notation) { return vim.feed(notation, ctx, host); }
  QString text() const { return doc.rope().toString(); }
  QString state() const { return render(text(), sel.selections()); }
};


struct Row {
  const char *name;
  const char *initial;
  const char *keys;
  const char *expected;
};

const Row k_insert[] = {
  {"i inserts", "ab|c", "ixy<Esc>", "abx|yc"},
  {"a appends", "ab|c", "axy<Esc>", "abcx|y"},
  {"I at first non-blank", "  a|bc", "Ix<Esc>", "  |xabc"},
  {"A at end", "|abc", "Ax<Esc>", "abc|x"},
  {"o opens below", "a|b\ncd", "ox<Esc>", "ab\n|x\ncd"},
  {"O opens above", "a|b\ncd", "Ox<Esc>", "|x\nab\ncd"},
  {"o keeps indent", "  a|b", "ox<Esc>", "  ab\n  |x"},
  {"count insert", "|", "3ia<Esc>", "aa|a"},
  {"count o", "|a", "2ob<Esc>", "a\nb\n|b"},
  {"esc at line start stays", "ab\n|cd", "ix<Esc>", "ab\n|xcd"},
  {"gI", "  a|b", "gIx<Esc>", "|x  ab"},
  {"backspace in insert", "|", "iabc<BS><Esc>", "a|b"},
  {"enter in insert", "|", "iab<CR>cd<Esc>", "ab\nc|d"},
  {"ctrl-w in insert", "|", "ifoo bar<C-w>x<Esc>", "foo |x"},
  {"clamp at line end", "ab|c", "l", "ab|c"},
  {"R replaces", "|abcd", "Rxy<Esc>", "x|ycd"},
  {"R backspace restores", "|abcd", "Rxyz<BS><Esc>", "x|ycd"},
  {"R past end appends", "a|b", "Rxyz<Esc>", "axy|z"},
  {"insert register", "|abc", "yliX<C-r>0<Esc>", "X|aabc"},
  {"ctrl-v literal", "|", "i<C-v>x<Esc>", "|x"},
  {"ctrl-u", "|", "iab cd<C-u>x<Esc>", "|x"},
  {"insert arrows", "|ab", "iX<Left><Left>Y<Esc>", "|YXab"},
  {"ctrl-o", "|ab", "iX<C-o>0Y<Esc>", "|YXab"},
  {"tab inserts indent", "|", "i<Tab>x<Esc>", "    |x"},
};

const Row k_motion[] = {
  {"l", "|abc", "l", "a|bc"},
  {"l count", "|abcd", "2l", "ab|cd"},
  {"h", "ab|c", "h", "a|bc"},
  {"h stops at line start", "|abc", "5h", "|abc"},
  {"j keeps column", "ab|cd\nefgh", "j", "abcd\nef|gh"},
  {"j goal column", "abc|d\nx\nabcdef", "jj", "abcd\nx\nabc|def"},
  {"dollar is sticky", "|abcd\nxy\nabcdef", "$jj", "abcd\nxy\nabcde|f"},
  {"k", "abcd\nef|gh", "k", "ab|cd\nefgh"},
  {"j at last line", "a|b", "j", "a|b"},
  {"w", "|foo bar", "w", "foo |bar"},
  {"w punct", "|foo.bar", "w", "foo|.bar"},
  {"W", "|foo.bar baz", "W", "foo.bar |baz"},
  {"w across lines", "fo|o\nbar", "w", "foo\n|bar"},
  {"w stops on empty line", "fo|o\n\nbar", "w", "foo\n|\nbar"},
  {"2w", "|a b c", "2w", "a b |c"},
  {"b", "foo ba|r", "b", "foo |bar"},
  {"b to prev word", "foo |bar", "b", "|foo bar"},
  {"e", "|foo bar", "e", "fo|o bar"},
  {"e from end of word", "fo|o bar", "e", "foo ba|r"},
  {"ge", "foo ba|r", "ge", "fo|o bar"},
  {"dollar", "ab|cd", "$", "abc|d"},
  {"zero", "ab|cd", "0", "|abcd"},
  {"caret", "  ab|cd", "^", "  |abcd"},
  {"gg", "a\nb\n|c", "gg", "|a\nb\nc"},
  {"G", "|a\nb\nc", "G", "a\nb\n|c"},
  {"3G", "|a\nb\nc\nd", "3G", "a\nb\n|c\nd"},
  {"f", "|a.b.c", "f.", "a|.b.c"},
  {"2f", "|a.b.c", "2f.", "a.b|.c"},
  {"F", "a.b.|c", "F.", "a.b|.c"},
  {"semicolon", "|a.b.c.d", "f.;", "a.b|.c.d"},
  {"f fails", "|abc", "fz", "|abc"},
  {"percent open", "|(a[b]c)", "%", "(a[b]c|)"},
  {"percent close", "(a[b]c|)", "%", "|(a[b]c)"},
  {"percent finds bracket after cursor", "|x(a)", "%", "x(a|)"},
  {"brace forward", "|a\nb\n\nc\nd", "}", "a\nb\n|\nc\nd"},
  {"brace back", "a\nb\n\nc\n|d", "{", "a\nb\n|\nc\nd"},
  {"space moves right", "|ab", "<Space>", "a|b"},
  {"sentence forward", "|Hello there. Next one.", ")", "Hello there. |Next one."},
  {"sentence back", "Hello there. Ne|xt one.", "(", "Hello there. |Next one."},
  {"bar column", "|abcdef", "4|", "abc|def"},
  {"minus", "a\n  b\n|c", "-", "a\n  |b\nc"},
  {"plus", "|a\n  b", "+", "a\n  |b"},
  {"underscore", "|a\n  b", "2_", "a\n  |b"},
  {"g_", "|ab  ", "g_", "a|b  "},
  {"backspace key", "a|b", "<BS>", "|ab"},
  {"percent count", "|a\nb\nc\nd", "50%", "a\n|b\nc\nd"},
  {"count with zero", "|abcdefghijkl", "10l", "abcdefghij|kl"},
};

const Row k_operators[] = {
  {"dw", "|foo bar", "dw", "|bar"},
  {"dw last word keeps line", "foo |bar\nbaz", "dw", "foo| \nbaz"},
  {"de", "|foo bar", "de", "| bar"},
  {"d dollar", "ab|cd", "d$", "a|b"},
  {"D", "ab|cd", "D", "a|b"},
  {"d0", "ab|cd", "d0", "|cd"},
  {"d caret", "  ab|cd", "d^", "  |cd"},
  {"dd", "a\n|b\nc", "dd", "a\n|c"},
  {"dd last line", "a\n|b", "dd", "|a"},
  {"dd only line", "|a", "dd", "|"},
  {"2dd", "|a\nb\nc", "2dd", "|c"},
  {"dd count too big", "|a\nb", "9dd", "|"},
  {"dj", "|a\nb\nc", "dj", "|c"},
  {"dk", "a\n|b\nc", "dk", "|c"},
  {"dG", "a\n|b\nc", "dG", "|a"},
  {"dgg", "a\n|b\nc", "dgg", "|c"},
  {"x", "a|bc", "x", "a|c"},
  {"x at end", "ab|c", "x", "a|b"},
  {"2x", "|abcd", "2x", "|cd"},
  {"x on empty line", "|\na", "x", "|\na"},
  {"X", "ab|c", "X", "a|c"},
  {"cw", "|foo bar", "cwx<Esc>", "|x bar"},
  {"cw on blank", "foo| bar", "cwx<Esc>", "foo|xbar"},
  {"cw count", "|a b c", "2cwx<Esc>", "|x c"},
  {"cc", "a\n  |b\nc", "ccx<Esc>", "a\n  |x\nc"},
  {"S", "a\n|b\nc", "Sx<Esc>", "a\n|x\nc"},
  {"C", "ab|cd", "Cx<Esc>", "ab|x"},
  {"s", "a|bc", "sx<Esc>", "a|xc"},
  {"ciw", "foo |bar baz", "ciwX<Esc>", "foo |X baz"},
  {"yy p", "|a\nb", "yyp", "a\n|a\nb"},
  {"yy P", "a\n|b", "yyP", "a\n|b\nb"},
  {"yy 2p", "|a\nb", "yy2p", "a\n|a\na\nb"},
  {"yw p", "|foo bar", "yw$p", "foo barfoo| "},
  {"yl 3p", "|a", "yl3p", "aaa|a"},
  {"p after last line", "a\n|b", "yyp", "a\nb\n|b"},
  {"yj cursor stays", "|a\nb\nc", "yj", "|a\nb\nc"},
  {"yk moves up", "a\n|b", "yk", "|a\nb"},
  {"tilde", "|abc", "~", "A|bc"},
  {"3 tilde", "|abc", "3~", "AB|C"},
  {"r", "|abc", "rx", "|xbc"},
  {"2r", "|abc", "2rx", "x|xc"},
  {"r too long fails", "|ab", "3rx", "|ab"},
  {"r enter", "a|bc", "r<CR>", "a\n|c"},
  {"J", "|a\nb", "J", "a| b"},
  {"J strips indent", "|a\n  b", "J", "a| b"},
  {"3J", "|a\n\nb", "3J", "a| b"},
  {"gJ", "|a\n b", "gJ", "a| b"},
  {"g tilde w", "|foo bar", "g~w", "|FOO bar"},
  {"gUiw", "foo |bar", "gUiw", "foo |BAR"},
  {"guu", "|FOO\nBAR", "guu", "|foo\nBAR"},
  {"gUU", "|foo\nbar", "gUU", "|FOO\nbar"},
  {"shift right", "|a\nb", ">>", "    |a\nb"},
  {"shift right count", "|a\nb\nc", "2>>", "    |a\n    b\nc"},
  {"shift left", "    |a", "<<", "|a"},
  {"shift right j", "|a\nb\nc", ">j", "    |a\n    b\nc"},
  {"increment", "a|9", "<C-a>", "a1|0"},
  {"decrement", "|10", "<C-x>", "|9"},
  {"increment count", "|1", "5<C-a>", "|6"},
  {"increment negative", "|-1", "<C-a>", "|0"},
  {"d count motion", "|a b c d e", "2d2w", "|e"},
  {"dfx", "|abcxd", "dfx", "|d"},
  {"dtx", "|abcxd", "dtx", "|xd"},
  {"d paragraph", "|a\nb\n\nc", "d}", "|\nc"},
  {"d percent", "|(a)b", "d%", "|b"},
  {"dh at column 0", "|ab", "dh", "|ab"},
  {"d search", "|foo bar", "d/bar<CR>", "|bar"},
};

const Row k_textobject[] = {
  {"diw", "foo |bar baz", "diw", "foo | baz"},
  {"daw", "foo |bar baz", "daw", "foo |baz"},
  {"daw at end takes leading blank", "foo |bar", "daw", "fo|o"},
  {"d2aw", "|a b c d", "d2aw", "|c d"},
  {"diW", "|a.b c", "diW", "| c"},
  {"di paren", "f(a, |b)", "di(", "f(|)"},
  {"da paren", "f(a,|b)", "da(", "|f"},
  {"dib nested", "f(a(b|c)d)", "dib", "f(a(|)d)"},
  {"d2i paren", "f(a(b|c)d)", "d2i(", "f(|)"},
  {"di bracket", "[a|b]", "di[", "[|]"},
  {"di brace one line", "{a|b}", "di{", "{|}"},
  {"di angle", "<a|b>", "di<", "<|>"},
  {"ci quote", "say \"he|llo\" now", "ci\"X<Esc>", "say \"|X\" now"},
  {"da quote", "say \"he|llo\" now", "da\"", "say |now"},
  {"di quote before", "|say \"hello\" now", "di\"", "say \"|\" now"},
  {"di single", "a 'b|c' d", "di'", "a '|' d"},
  {"dip", "a\nb|\n\nc", "dip", "|\nc"},
  {"dap", "a\nb|\n\nc", "dap", "|c"},
  {"yip G p", "a\n|b\n\nc", "yipGp", "a\nb\n\nc\n|a\nb"},
  {"di brace multi-line", "f {\n  a|\n}", "di{", "f {\n|}"},
  {"ci brace multi-line", "f {\n  a|\n}", "ci{x<Esc>", "f {\n  |x\n}"},
  {"dit", "<a><b>x|y</b></a>", "dit", "<a><b>|</b></a>"},
  {"dat", "<a><b>x|y</b></a>", "dat", "<a>|</a>"},
  {"dis", "One. T|wo. Three.", "dis", "One. | Three."},
  {"das", "One. T|wo. Three.", "das", "One. |Three."},
};

const Row k_undo[] = {
  {"undo insert", "|abc", "ixy<Esc>u", "|abc"},
  {"undo x", "|abc", "xu", "|abc"},
  {"undo twice", "|abc", "xxuu", "|abc"},
  {"redo", "|abc", "xu<C-r>", "|bc"},
  {"undo cw is one step", "|foo bar", "cwX<Esc>u", "|foo bar"},
  {"undo o is one step", "|a", "ob<Esc>u", "|a"},
  {"undo dd", "a\n|b\nc", "ddu", "a\n|b\nc"},
  {"undo count", "|abc", "3xu", "|abc"},
  {"u at oldest", "|abc", "u", "|abc"},
  {"undo visual delete", "|abc", "vldu", "|abc"},
};

const Row k_dot[] = {
  {"dot dw", "|a b c d", "dw.", "|c d"},
  {"dot x", "|abcd", "x..", "|d"},
  {"dot insert", "|x", "ihi<Esc>.", "hh|iix"},
  {"dot append", "|a\nb", "A;<Esc>j.", "a;\nb|;"},
  {"dot cw", "|foo bar baz", "cwX<Esc>w.", "X |X baz"},
  {"dot with count", "|a b c d e", "dw2.", "|d e"},
  {"dot dd", "|a\nb\nc", "dd.", "|c"},
  {"dot o", "|a", "ob<Esc>.", "a\nb\n|b"},
  {"dot p", "|a", "ylp.", "aa|a"},
  {"dot r", "|abc", "rxl.", "x|xc"},
  {"dot tilde", "|abcd", "~.", "AB|cd"},
  {"dot shift", "|a", ">>.", "        |a"},
  {"dot visual", "|abcdef", "vld.", "|ef"},
  {"dot visual line", "|a\nb\nc\nd", "Vjd.", "|"},
  {"dot J", "|a\nb\nc", "J.", "a b| c"},
  {"dot ciw", "|foo bar", "ciwX<Esc>w.", "X |X"},
  {"dot count replaces", "|abcdef", "2x3.", "|f"},
  {"dot after undo", "|abcd", "xu.", "|bcd"},
};

const Row k_visual[] = {
  {"v l", "|abcd", "vl", "‹ab›cd"},
  {"v j", "a|bc\nxyz", "vj", "a‹bc\nxy›z"},
  {"v dollar includes newline", "|ab\ncd", "v$", "‹ab\n›cd"},
  {"v esc", "|abc", "vl<Esc>", "a|bc"},
  {"v backwards", "ab|cd", "vh", "a‹bc›d"},
  {"v d", "|abcd", "vld", "|cd"},
  {"v y", "|abcd", "vly", "|abcd"},
  {"v y then P", "|ab", "vlyP", "a|bab"},
  {"v c", "|abcd", "vlcx<Esc>", "|xcd"},
  {"v tilde", "|abc", "vl~", "|ABc"},
  {"v U", "|abc", "vlU", "|ABc"},
  {"v u", "|ABC", "vlu", "|abC"},
  {"V", "a\n|b\nc", "V", "a\n‹b\n›c"},
  {"V d", "a\n|b\nc", "Vd", "a\n|c"},
  {"V j d", "|a\nb\nc", "Vjd", "|c"},
  {"V y p", "|a\nb", "Vyp", "a\n|a\nb"},
  {"V shift", "|a\nb", "Vj>", "    |a\n    b"},
  {"V c", "a\n|b\nc", "Vcx<Esc>", "a\n|x\nc"},
  {"V J", "|a\nb\nc", "VjJ", "a| b\nc"},
  {"viw", "foo |bar baz", "viw", "foo ‹bar› baz"},
  {"vaw", "foo |bar baz", "vaw", "foo ‹bar ›baz"},
  {"vi paren", "f(a|b)", "vi(", "f(‹ab›)"},
  {"viwiw extends", "|foo bar", "viwiw", "‹foo ›bar"},
  {"vip", "a\n|b\n\nc", "vip", "‹a\nb\n›\nc"},
  {"v o swaps", "|abcd", "vlol", "a‹b›cd"},
  {"v p replaces", "|foo bar", "yiwwviwp", "foo fo|o"},
  {"v p fills unnamed", "|a b", "yiwwviwpp", "a a|b"},
  {"v r", "|abc", "vlrx", "|xxc"},
  {"v I", "a|bc", "vlIx<Esc>", "a|xbc"},
  {"v A", "a|bcd", "vlAx<Esc>", "abc|xd"},
  {"gv", "|abc", "vl<Esc>gv", "‹ab›c"},
  {"mouse selection then d", "‹ab›cd", "d", "|cd"},
  {"v gu", "|ABC", "vlgu", "|abC"},
  {"V count shift", "|a", "V3>", "            |a"},
  {"v search extends", "|a b a", "v/a<CR>", "‹a b a›"},
  {"v e", "|foo bar", "ve", "‹foo› bar"},
  {"v colon range", "|a\nb\nc", "Vj:s/$/;/<CR>", "a;\n|b;\nc"},
};

const Row k_block[] = {
  {"block select", "|abc\ndef", "<C-v>jl", "‹ab›c\n‹de›f"},
  {"block d", "|abc\ndef\nghi", "<C-v>jld", "|c\nf\nghi"},
  {"block I", "|abc\ndef\nghi", "<C-v>jIx<Esc>", "|xabc\nxdef\nghi"},
  {"block A", "|abc\ndef\nghi", "<C-v>jAx<Esc>", "|axbc\ndxef\nghi"},
  {"block dollar A", "|abc\ndef\nghi", "<C-v>j$Ax<Esc>", "|abcx\ndefx\nghi"},
  {"block c", "|abc\ndef\nghi", "<C-v>jcX<Esc>", "|Xbc\nXef\nghi"},
  {"block r", "|abc\ndef\nghi", "<C-v>jlr-", "|--c\n--f\nghi"},
  {"block y P", "|ab\ncd", "<C-v>jlyP", "|abab\ncdcd"},
  {"block short row", "|abc\nd\nefg", "<C-v>jjld", "|c\n\ng"},
  {"block I skips short", "ab|c\nd\nefg", "<C-v>jjIx<Esc>", "ab|xc\nd\nefxg"},
  {"block tilde", "|ab\ncd", "<C-v>jl~", "|AB\nCD"},
  {"block shift", "|a\nb", "<C-v>j>", "    |a\n    b"},
};

const Row k_registers[] = {
  {"named yank paste", "|foo bar", "\"ayw$\"ap", "foo barfoo| "},
  {"append register", "|a\nb", "\"ayyj\"Ayy\"ap", "a\nb\n|a\nb"},
  {"numbered delete", "|a\nb\nc", "dddd\"2p", "c\n|a"},
  {"numbered one", "|a\nb\nc", "dddd\"1p", "c\n|b"},
  {"small delete register", "|abc", "x\"-p", "b|ac"},
  {"register zero survives delete", "|a b", "yiwwdiw\"0P", "a|a "},
  {"black hole", "|a b", "yiww\"_diwP", "a|a "},
  {"unnamed after delete", "|a b", "dwP", "a| b"},
  {"count p linewise", "|a", "yy3p", "a\n|a\na\na"},
  {"P linewise first column", "|  a", "yyP", "  |a\n  a"},
  {"charwise multi-line p", "|a\nb", "vjyp", "a|a\nb\nb"},
  {"p empty register", "|a", "\"zp", "|a"},
  {"dd then p", "a\n|b\nc", "ddp", "a\nc\n|b"},
  {"xp swaps", "|ab", "xp", "b|a"},
  {"ddp swaps lines", "|a\nb", "ddp", "b\n|a"},
};

const Row k_search[] = {
  {"slash", "|foo bar foo", "/foo<CR>", "foo bar |foo"},
  {"slash wraps", "foo bar |foo", "/bar<CR>", "foo |bar foo"},
  {"n", "|a b a b a", "/a<CR>n", "a b a b |a"},
  {"N", "|a b a b a", "/a<CR>N", "|a b a b a"},
  {"question", "foo bar |foo", "?bar<CR>", "foo |bar foo"},
  {"n after question goes back", "a b a b |a", "?a<CR>n", "|a b a b a"},
  {"star", "|foo bar foo", "*", "foo bar |foo"},
  {"star whole word", "|a ab a", "*", "a ab |a"},
  {"hash", "foo bar |foo", "#", "|foo bar foo"},
  {"search count", "|a a a a", "3/a<CR>", "a a a |a"},
  {"search not found", "|abc", "/zzz<CR>", "|abc"},
  {"search regex", "|a1 b22", "/\\d\\+<CR>", "a|1 b22"},
  {"search very magic", "|a1 b22", "/\\v\\d{2}<CR>", "a1 b|22"},
  {"search word boundary", "|cat concat cat", "/\\<cat<CR>", "cat concat |cat"},
  {"search ignorecase flag", "|Foo foo", "/\\cfoo<CR>", "Foo |foo"},
  {"search alternation", "|x a y b", "/a\\|b<CR>", "x |a y b"},
  {"search anchor", "a\n|b\nab", "/^a<CR>", "a\nb\n|ab"},
  {"search BS edits", "|abc abd", "/abx<BS>d<CR>", "abc |abd"},
  {"search esc cancels", "|abc", "/b<Esc>", "|abc"},
  {"operator search", "|a b c", "d/c<CR>", "|c"},
};

const Row k_marks[] = {
  {"mark d", "|abcd", "lma$d`a", "a|d"},
  {"mark line", "|a\nb\nc", "majjd'a", "|"},
  {"mark jump back", "a\nb|\nc", "majj'a", "a\n|b\nc"},
  {"backtick backtick", "|a\nb\nc", "G``", "|a\nb\nc"},
  {"ctrl-o", "|a\nb\nc\nd", "G<C-o>", "|a\nb\nc\nd"},
  {"ctrl-i", "|a\nb\nc\nd", "G<C-o><C-i>", "a\nb\nc\n|d"},
  {"mark follows insertion", "a|b", "maIx<Esc>`a", "xa|b"},
  {"mark unset", "|ab", "l'z", "a|b"},
};

const Row k_macro[] = {
  {"record and play", "|a\nb\nc", "qaA!<Esc>jq@a", "a!\nb!\n|c"},
  {"play count", "|a\nb\nc\nd", "qaA!<Esc>jq3@a", "a!\nb!\nc!\nd|!"},
  {"at at", "|a\nb\nc\nd", "qaA!<Esc>jq@a@@", "a!\nb!\nc!\n|d"},
  {"macro one undo", "|a\nb\nc", "qaA!<Esc>jq@au", "a!\n|b\nc"},
  {"macro stops on failed motion", "|a\nb", "qa0jq@a", "a\n|b"},
  {"macro text register", "|x", "qaiab<Esc>q", "a|bx"},
};

const Row k_multi[] = {
  {"x on two cursors", "|ab\n|cd", "x", "|b\n|d"},
  {"dw on two", "|a b\n|c d", "dw", "|b\n|d"},
  {"insert on two", "|a\n|b", "ihi<Esc>", "h|ia\nh|ib"},
  {"esc collapses", "|a\n|b", "<Esc>", "|a\nb"},
  {"dot on two", "|a b\n|c d", "dw.", "|\n|"},
  {"w moves both", "|a b\n|c d", "w", "a |b\nc |d"},
  {"merge", "|a|b", "x", "|"},
  {"paste distributes", "|a\n|b", "ylp", "a|a\nb|b"},
  {"o on two", "|a\n|b", "ox<Esc>", "a\n|x\nb\n|x"},
  {"shift two", "|a\n|b", ">>", "    |a\n    |b"},
};

const Row k_ex[] = {
  {"s", "|aaa", ":s/a/b/<CR>", "|baa"},
  {"s g", "|aaa", ":s/a/b/g<CR>", "|bbb"},
  {"s percent", "|aa\naa", ":%s/a/b/g<CR>", "bb\n|bb"},
  {"s group", "|ab", ":s/\\(a\\)b/\\1x/<CR>", "|ax"},
  {"s amp", "|ab", ":s/a/[&]/<CR>", "|[a]b"},
  {"s newline", "|a b", ":s/ /\\r/<CR>", "a\n|b"},
  {"s dollar", "|a", ":s/$/;/<CR>", "|a;"},
  {"s caret", "|a", ":s/^/# /<CR>", "|# a"},
  {"s other delimiter", "|a/b", ":s#/#-#<CR>", "|a-b"},
  {"s range", "|x\nx\nx", ":1,2s/x/y/<CR>", "y\n|y\nx"},
  {"s case flag", "|Aa", ":s/a/b/gi<CR>", "|bb"},
  {"s upper", "|ab", ":s/a/\\U&/<CR>", "|Ab"},
  {"s not found", "|ab", ":s/z/y/<CR>", "|ab"},
  {"d", "a\n|b\nc", ":d<CR>", "a\n|c"},
  {"2d", "|a\nb\nc", ":2d<CR>", "a\n|c"},
  {"range d", "|a\nb\nc\nd", ":2,3d<CR>", "a\n|d"},
  {"d register", "|a\nb", ":d x<CR>\"xp", "b\n|a"},
  {"goto line", "|a\nb\nc", ":3<CR>", "a\nb\n|c"},
  {"goto dollar", "|a\nb\nc", ":$<CR>", "a\nb\n|c"},
  {"goto relative", "|a\nb\nc", ":+1<CR>", "a\n|b\nc"},
  {"g d", "|a\nb\na\nc", ":g/a/d<CR>", "b\nc"},
  {"v d", "|a\nb\na\nc", ":v/a/d<CR>", "a\na"},
  {"g s", "|a\nab\nb", ":g/b/s/b/X/<CR>", "a\naX\nX"},
  {"g normal", "|a\nb", ":g/./normal Ax<CR>", "ax\nbx"},
  {"normal range", "|a\nb", ":%normal Ax<CR>", "ax\nbx"},
  {"g d one undo", "|a\nb\na", ":g/a/d<CR>u", "a\nb\na"},
  {"marks range", "|a\nb\nc\nd", "majjmb:'a,'bd<CR>", "d"},
  {"search range", "|a\nb\nc\nd", ":/c/d<CR>", "a\nb\nd"},
  {"yank ex", "|a\nb", ":y<CR>p", "a\n|a\nb"},
  {"shift ex", "|a", ":><CR>", "    |a"},
  {"set ic", "|a A", ":set ic<CR>/A<CR>", "a |A"},
  {"colon esc cancels", "|a", ":s/a/b/<Esc>", "|a"},
  {"colon BS", "|aa", ":s/a/b/x<BS><CR>", "|ba"},
  {"V colon d", "|a\nb\nc", "Vj:d<CR>", "|c"},
};

const Row k_misc[] = {
  {"count zero is motion", "|abc", "$0", "|abc"},
  {"insert ctrl-r register", "|ab", "yiwA <C-r>\"<Esc>", "ab a|b"},
  {"autoclose in insert", "|", "i(<Esc>", "|()"},
  {"unknown key does nothing", "|abc", "Q", "|abc"},
};

const Row k_extra[] = {
  {"goal resets after horizontal move", "|abcdefgh\nabcdefgh\nabcdefgh", "jlllj", "abcdefgh\nabcdefgh\nabc|defgh"},
  {"goal resets after word move", "|abcdefgh\nab cdefgh\nabcdefgh", "jwj", "abcdefgh\nab cdefgh\nabc|defgh"},
  {"failed motion keeps the goal", "abc|defgh\nabcdefgh", "jjk", "abc|defgh\nabcdefgh"},
  {"goal kept across short row", "abc|defgh\nab\nabcdefgh", "jj", "abcdefgh\nab\nabc|defgh"},
  {"goal after visual move", "|abcdefgh\nabcdefgh\nabcdefgh", "vjlllj<Esc>", "abcdefgh\nabcdefgh\nabc|defgh"},
  {"block j keeps column after l", "|abcdefgh\nabcdefgh\nabcdefgh", "<C-v>jlllj", "‹abcd›efgh\n‹abcd›efgh\n‹abcd›efgh"},
  {"block j then l then j", "|abcdefgh\nabcdefgh\nabcdefgh", "<C-v>jlllj", "‹abcd›efgh\n‹abcd›efgh\n‹abcd›efgh"},
  {"block j from zero column then l", "|abcdefgh\nabcdefgh\nabcdefgh\nabcdefgh", "<C-v>jllljj", "‹abcd›efgh\n‹abcd›efgh\n‹abcd›efgh\n‹abcd›efgh"},
  {"cw at word end", "fo|o bar", "cwX<Esc>", "fo|X bar"},
  {"cW", "|a.b c", "cWX<Esc>", "|X c"},
  {"2cc", "|a\nb\nc", "2ccX<Esc>", "|X\nc"},
  {"c dollar keeps line", "a|bc\nd", "c$X<Esc>", "a|X\nd"},
  {"dot A with j", "|a\nb", "A!<Esc>j.", "a!\nb|!"},
  {"3 dot", "|abcdef", "x3.", "|ef"},
  {"undo dot", "|abcd", "x.u", "|bcd"},
  {"emoji l", "|😀a", "l", "😀|a"},
  {"emoji x", "|😀a", "x", "|a"},
  {"emoji r", "|😀a", "rx", "|xa"},
  {"CRLF dd", "a\r\n|b\r\nc", "dd", "a\r\n|c"},
  {"CRLF j", "a|b\r\ncd", "j", "ab\r\nc|d"},
  {"CRLF dollar", "|ab\r\ncd", "$", "a|b\r\ncd"},
  {"dt paren", "|f(a) b", "dt)", "|) b"},
  {"d semicolon", "|a,b,c,d", "f,d;", "a|c,d"},
  {"tilde at end", "a|b", "3~", "a|B"},
  {"2J", "|a\nb\nc", "2J", "a| b\nc"},
  {"block dollar d", "|abc\nde\nfghi", "<C-v>jj$d", "|\n\n"},
  {"block insert multi char", "|ab\ncd", "<C-v>jIxy<Esc>", "|xyab\nxycd"},
  {"iw on punctuation", "a.|.b", "diw", "a|b"},
  {"ci paren across lines", "f(\n  a|,\n  b\n)", "ci(X<Esc>", "f(\n  |X\n)"},
  {"dap at end takes blank before", "a\n\nb|", "dap", "|a"},
  {"yiw P", "|foo bar", "yiwwP", "foo fo|obar"},
  {"count A", "|a", "2A!<Esc>", "a!|!"},
  {"2S", "|a\nb\nc", "2Sx<Esc>", "|x\nc"},
  {"X at line start", "|ab", "X", "|ab"},
  {"2D", "|ab\ncd\nef", "2D", "|\nef"},
  {"delete then P", "|a\nb", "yyjddP", "|b\na"},
  {"recursive macro terminates", "|a", "qaq@a", "|a"},
  {"search then cw dot", "|a b a b", "/b<CR>cwX<Esc>n.", "a X a |X"},
  {"yank in visual block then p", "|ab\ncd\nef", "<C-v>jyGp", "ab\ncd\ne|af\n c"},
  {"gv after d", "|abc", "vld<Esc>gvd", "|"},
  {"o undo redo", "|a", "ob<Esc>u<C-r>", "a\n|b"},
  {"p undo", "|a", "yyp", "a\n|a"},
  {"x then p then u", "|ab", "xpu", "|b"},
  {"n counted", "|a a a a", "/a<CR>2n", "a a a |a"},
  {"backward word count", "a b c |d", "3b", "|a b c d"},
  {"e count", "|a b c", "2e", "a b |c"},
  {"dgE", "a b| c", "dgE", "a |c"},
  {"star on punctuation", "|a.b a.b", "*", "a.b |a.b"},
  {"hash backward word", "a b a |b", "#", "a |b a b"},
};

void addRows(const Row *rows, size_t count) {
  QTest::addColumn<QString>("initial");
  QTest::addColumn<QString>("keys");
  QTest::addColumn<QString>("expected");
  for (size_t i = 0; i < count; ++i)
    QTest::newRow(rows[i].name) << QString::fromUtf8(rows[i].initial) << QString::fromUtf8(rows[i].keys)
                                << QString::fromUtf8(rows[i].expected);
}

void runRow(const QString &initial, const QString &keys, const QString &expected) {
  Fixture f(initial);
  f.keys(keys);
  const bool markers = expected.contains(u'|') || expected.contains(u'‹');
  QCOMPARE(markers ? f.state() : f.text(), expected);
}

} // namespace

class TstVim : public QObject {
  Q_OBJECT
private slots:
  void insert_data() { addRows(k_insert, std::size(k_insert)); }
  void insert() {
    QFETCH(QString, initial);
    QFETCH(QString, keys);
    QFETCH(QString, expected);
    runRow(initial, keys, expected);
  }
  void motion_data() { addRows(k_motion, std::size(k_motion)); }
  void motion() {
    QFETCH(QString, initial);
    QFETCH(QString, keys);
    QFETCH(QString, expected);
    runRow(initial, keys, expected);
  }
  void operators_data() { addRows(k_operators, std::size(k_operators)); }
  void operators() {
    QFETCH(QString, initial);
    QFETCH(QString, keys);
    QFETCH(QString, expected);
    runRow(initial, keys, expected);
  }
  void textobject_data() { addRows(k_textobject, std::size(k_textobject)); }
  void textobject() {
    QFETCH(QString, initial);
    QFETCH(QString, keys);
    QFETCH(QString, expected);
    runRow(initial, keys, expected);
  }
  void undo_data() { addRows(k_undo, std::size(k_undo)); }
  void undo() {
    QFETCH(QString, initial);
    QFETCH(QString, keys);
    QFETCH(QString, expected);
    runRow(initial, keys, expected);
  }
  void dot_data() { addRows(k_dot, std::size(k_dot)); }
  void dot() {
    QFETCH(QString, initial);
    QFETCH(QString, keys);
    QFETCH(QString, expected);
    runRow(initial, keys, expected);
  }
  void visual_data() { addRows(k_visual, std::size(k_visual)); }
  void visual() {
    QFETCH(QString, initial);
    QFETCH(QString, keys);
    QFETCH(QString, expected);
    runRow(initial, keys, expected);
  }
  void block_data() { addRows(k_block, std::size(k_block)); }
  void block() {
    QFETCH(QString, initial);
    QFETCH(QString, keys);
    QFETCH(QString, expected);
    runRow(initial, keys, expected);
  }
  void registers_data() { addRows(k_registers, std::size(k_registers)); }
  void registers() {
    QFETCH(QString, initial);
    QFETCH(QString, keys);
    QFETCH(QString, expected);
    runRow(initial, keys, expected);
  }
  void search_data() { addRows(k_search, std::size(k_search)); }
  void search() {
    QFETCH(QString, initial);
    QFETCH(QString, keys);
    QFETCH(QString, expected);
    runRow(initial, keys, expected);
  }
  void marks_data() { addRows(k_marks, std::size(k_marks)); }
  void marks() {
    QFETCH(QString, initial);
    QFETCH(QString, keys);
    QFETCH(QString, expected);
    runRow(initial, keys, expected);
  }
  void macro_data() { addRows(k_macro, std::size(k_macro)); }
  void macro() {
    QFETCH(QString, initial);
    QFETCH(QString, keys);
    QFETCH(QString, expected);
    runRow(initial, keys, expected);
  }
  void multi_data() { addRows(k_multi, std::size(k_multi)); }
  void multi() {
    QFETCH(QString, initial);
    QFETCH(QString, keys);
    QFETCH(QString, expected);
    runRow(initial, keys, expected);
  }
  void ex_data() { addRows(k_ex, std::size(k_ex)); }
  void ex() {
    QFETCH(QString, initial);
    QFETCH(QString, keys);
    QFETCH(QString, expected);
    runRow(initial, keys, expected);
  }
  void misc_data() { addRows(k_misc, std::size(k_misc)); }
  void misc() {
    QFETCH(QString, initial);
    QFETCH(QString, keys);
    QFETCH(QString, expected);
    runRow(initial, keys, expected);
  }

  void extra_data() { addRows(k_extra, std::size(k_extra)); }
  void extra() {
    QFETCH(QString, initial);
    QFETCH(QString, keys);
    QFETCH(QString, expected);
    runRow(initial, keys, expected);
  }

  void modes() {
    Fixture f(u"|abc"_s);
    QCOMPARE(f.vim.mode(), VimInputHandler::Mode::Normal);
    f.keys(u"v"_s);
    QCOMPARE(f.vim.mode(), VimInputHandler::Mode::Visual);
    f.keys(u"<Esc>V"_s);
    QCOMPARE(f.vim.mode(), VimInputHandler::Mode::VisualLine);
    f.keys(u"<Esc><C-v>"_s);
    QCOMPARE(f.vim.mode(), VimInputHandler::Mode::VisualBlock);
    f.keys(u"<Esc>i"_s);
    QCOMPARE(f.vim.mode(), VimInputHandler::Mode::Insert);
    QVERIFY(f.vim.acceptsTextInput());
    QCOMPARE(f.vim.cursorShape(), CursorShape::Line);
    f.keys(u"<Esc>R"_s);
    QCOMPARE(f.vim.mode(), VimInputHandler::Mode::Replace);
    QCOMPARE(f.vim.cursorShape(), CursorShape::Underline);
    f.keys(u"<Esc>d"_s);
    QCOMPARE(f.vim.mode(), VimInputHandler::Mode::OperatorPending);
    QCOMPARE(f.vim.pendingKeys(), u"d"_s);
    f.keys(u"<Esc>:s/a"_s);
    QCOMPARE(f.vim.mode(), VimInputHandler::Mode::CommandLine);
    QCOMPARE(f.vim.commandLine(), u":s/a"_s);
    f.keys(u"<Esc>"_s);
    QCOMPARE(f.vim.mode(), VimInputHandler::Mode::Normal);
    QVERIFY(!f.vim.acceptsTextInput());
    QCOMPARE(f.vim.cursorShape(), CursorShape::Block);
    QCOMPARE(f.vim.modeName(), u"NORMAL"_s);
    f.keys(u"qa"_s);
    QCOMPARE(f.vim.recordingRegister(), u"a"_s);
    f.keys(u"q"_s);
    QVERIFY(f.vim.recordingRegister().isEmpty());
  }

  void keyEvents() {
    Fixture f(u"|abc"_s);
    auto press = [&](Qt::Key key, Qt::KeyboardModifiers mods, const QString &text) {
      QKeyEvent event(QEvent::KeyPress, key, mods, text);
      return f.vim.keyPress(&event, f.ctx, f.host);
    };
    QVERIFY(press(Qt::Key_X, {}, u"x"_s));
    QCOMPARE(f.text(), u"bc"_s);
    QVERIFY(press(Qt::Key_I, {}, u"i"_s));
    QVERIFY(press(Qt::Key_Z, {}, u"z"_s));
    QVERIFY(press(Qt::Key_Escape, {}, u"\x1b"_s));
    QCOMPARE(f.text(), u"zbc"_s);
    QVERIFY(press(Qt::Key_R, Qt::ControlModifier, u"\x12"_s)); // redo
    QVERIFY(!press(Qt::Key_S, Qt::ControlModifier, u"\x13"_s)); // not vim's: left to the host
    QVERIFY(press(Qt::Key_Q, {}, u"q"_s)); // swallowed, not typed
    QVERIFY(press(Qt::Key_Q, {}, u"q"_s));
  }

  void textInputCommit() {
    Fixture f(u"|abc"_s);
    QVERIFY(!f.vim.commitText(u"x"_s, f.ctx, f.host)); // normal mode: not text
    f.keys(u"i"_s);
    QVERIFY(f.vim.commitText(u"xy"_s, f.ctx, f.host));
    f.keys(u"<Esc>"_s);
    QCOMPARE(f.text(), u"xyabc"_s);
    f.keys(u"0."_s); // dot repeats what an input method committed
    QCOMPARE(f.text(), u"xyxyabc"_s);
  }

  void cursorOffsets() {
    Fixture f(u"|abcd"_s);
    f.keys(u"vl"_s);
    QCOMPARE(f.vim.cursorOffset(0, f.sel.at(0), f.ctx), qsizetype(1)); // the end that moves
    f.keys(u"<Esc>"_s);
    QCOMPARE(f.vim.cursorOffset(0, f.sel.at(0), f.ctx), qsizetype(1));
    Fixture g(u"|abc\ndef"_s);
    g.keys(u"<C-v>j"_s);
    QCOMPARE(g.sel.count(), 2);
    int drawn = 0;
    for (int i = 0; i < g.sel.count(); ++i)
      drawn += g.vim.cursorOffset(i, g.sel.at(i), g.ctx) >= 0;
    QCOMPARE(drawn, 1); // one cursor for the block
  }

  void hostSignals() {
    Fixture f(u"|abc"_s);
    QSignalSpy write(&f.vim, &VimInputHandler::writeRequested);
    QSignalSpy quit(&f.vim, &VimInputHandler::quitRequested);
    QSignalSpy other(&f.vim, &VimInputHandler::exCommand);
    f.keys(u":w out.txt<CR>"_s);
    QCOMPARE(write.size(), 1);
    QCOMPARE(write.first().first().toString(), u"out.txt"_s);
    f.keys(u":q!<CR>"_s);
    QCOMPARE(quit.size(), 1);
    QCOMPARE(quit.first().first().toBool(), true);
    f.keys(u":wq<CR>"_s);
    QCOMPARE(write.size(), 2);
    QCOMPARE(quit.size(), 2);
    f.keys(u"ZZ"_s);
    QCOMPARE(write.size(), 3);
    f.keys(u":frobnicate now<CR>"_s);
    QCOMPARE(other.size(), 1);
    QCOMPARE(other.first().first().toString(), u"frobnicate now"_s);
  }

  void clipboardRegisters() {
    Fixture f(u"|foo bar"_s);
    f.keys(u"\"+yw"_s);
    QCOMPARE(f.host.clipboard, u"foo "_s);
    f.keys(u"$\"+p"_s);
    QCOMPARE(f.text(), u"foo barfoo "_s);
    f.host.selectionClipboard = u"S"_s;
    f.keys(u"0\"*P"_s);
    QCOMPARE(f.text(), u"Sfoo barfoo "_s);
  }

  void searchHighlight() {
    Fixture f(u"|foo bar"_s);
    f.keys(u"/bar<CR>"_s);
    QCOMPARE(f.host.highlight.pattern(), u"bar"_s);
    f.keys(u":noh<CR>"_s);
    QVERIFY(!f.host.highlight.isValid() || f.host.highlight.pattern().isEmpty());
    f.keys(u"n"_s);
    QCOMPARE(f.host.highlight.pattern(), u"bar"_s);
    QCOMPARE(f.vim.message(), QString());
    f.keys(u"/zzz<CR>"_s);
    QVERIFY(f.vim.message().startsWith(u"E486"_s));
  }

  void screenMotions() {
    Fixture f(u"|a\n  b\nc\nd\ne"_s);
    f.host.rows = {1, 3, true};
    f.keys(u"H"_s);
    QCOMPARE(f.state(), u"a\n  |b\nc\nd\ne"_s);
    f.keys(u"L"_s);
    QCOMPARE(f.state(), u"a\n  b\nc\n|d\ne"_s);
    f.keys(u"M"_s);
    QCOMPARE(f.state(), u"a\n  b\n|c\nd\ne"_s);
    f.keys(u"<C-d>"_s);
    QCOMPARE(f.host.scrolled, qsizetype(2));
    f.keys(u"<C-e>"_s);
    QCOMPARE(f.host.scrolled, qsizetype(3));
  }

  void foldAndDiagnosticKeys() {
    Fixture f(u"|a"_s);
    f.keys(u"zczozMzR]d[d"_s);
    QCOMPARE(f.host.folds, (QList<FoldCommand>{FoldCommand::FoldAtCursor, FoldCommand::UnfoldAtCursor, FoldCommand::FoldAll,
                                                FoldCommand::UnfoldAll}));
    QCOMPARE(f.host.diagnostics, (QList<bool>{true, false}));
  }

  void deactivateLeavesCursors() {
    Fixture f(u"a|bc"_s);
    f.keys(u"ix"_s); // an open insert group
    f.vim.deactivate(f.ctx, f.host);
    QVERIFY(f.doc.canUndo());
    QCOMPARE(f.sel.count(), 1);
    f.keys(u"u"_s); // not vim any more: ignored, not crashing
  }

  void insertSessionIsOneUndoStep() {
    Fixture f(u"|"_s);
    f.keys(u"ione<CR>two<Esc>"_s);
    QCOMPARE(f.text(), u"one\ntwo"_s);
    QVERIFY(f.doc.canUndo());
    f.keys(u"u"_s);
    QCOMPARE(f.text(), QString());
  }

  void externalSelectionBecomesVisual() {
    Fixture f(u"‹ab›cd"_s);
    f.keys(u"l"_s);
    QCOMPARE(f.vim.mode(), VimInputHandler::Mode::Visual);
    f.keys(u"<Esc>"_s);
    QCOMPARE(f.vim.mode(), VimInputHandler::Mode::Normal);
  }

  void macroRegisterIsText() {
    Fixture f(u"|x"_s);
    f.keys(u"qaA!<Esc>q"_s);
    QCOMPARE(f.vim.readRegister(u'a').text, u"A!<Esc>"_s);
    f.vim.setRegister(u'b', u"ay<Esc>"_s);
    f.keys(u"@b"_s);
    QCOMPARE(f.text(), u"x!y"_s);
  }

  void randomKeysDoNotCrash() {
    // Fuzz: random keys on a small document must leave a consistent selection set.
    const QStringList pool = {
      u"h"_s, u"j"_s, u"k"_s, u"l"_s, u"w"_s, u"b"_s, u"e"_s, u"x"_s, u"d"_s, u"c"_s, u"y"_s, u"p"_s, u"P"_s, u"u"_s,
      u"."_s, u"i"_s, u"a"_s, u"o"_s, u"v"_s, u"V"_s, u"<C-v>"_s, u"<Esc>"_s, u"$"_s, u"0"_s, u"G"_s, u"g"_s, u"f"_s,
      u"t"_s, u"3"_s, u"2"_s, u"q"_s, u"@"_s, u"m"_s, u"'"_s, u"%"_s, u"{"_s, u"}"_s, u"x"_s, u"w"_s, u"i"_s, u"("_s,
      u")"_s, u"\""_s, u"r"_s, u"J"_s, u"~"_s, u"<"_s, u">"_s, u"A"_s, u"I"_s, u"O"_s, u"/"_s, u"<CR>"_s, u"n"_s,
      u"*"_s, u"<BS>"_s, u"a"_s, u"b"_s, u" "_s, u"\n"_s, u"s"_s, u"S"_s, u"C"_s, u"D"_s, u"R"_s, u"<C-r>"_s,
    };
    std::mt19937 rng(qce::test::testSeed(7));
    for (int round = 0; round < qce::test::testIterations(40); ++round) {
      Fixture f(u"|foo bar (baz)\n  \"qux\" 12\n\nlast {line}"_s);
      for (int i = 0; i < 400; ++i) {
        f.vim.feed(pool[rng() % pool.size()], f.ctx, f.host);
        QVERIFY(f.sel.count() >= 1);
        for (int s = 0; s < f.sel.count(); ++s) {
          QVERIFY(f.sel.at(s).head >= 0 && f.sel.at(s).head <= f.doc.length());
          QVERIFY(f.sel.at(s).anchor >= 0 && f.sel.at(s).anchor <= f.doc.length());
        }
      }
      f.vim.feed(u"<Esc><Esc>"_s, f.ctx, f.host);
    }
  }
};

QTEST_APPLESS_MAIN(TstVim)
#include "tst_vim.moc"
