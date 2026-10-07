#include "core/vim/vimhandler.h"

#include "core/bracketmatch.h"
#include "core/textboundaries.h"
#include "core/vim/vimkeys.h"
#include "core/vim/vimmotions.h"
#include "core/vim/vimtextobjects.h"

#include <QtCore/QSet>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace qce::vim;
using namespace Qt::StringLiterals;

namespace qce {

namespace {

constexpr int kMaxReplayDepth = 20;
constexpr int kMaxCount = 99999999;

bool isVisualMode(VimInputHandler::Mode m) {
  return m == VimInputHandler::Mode::Visual || m == VimInputHandler::Mode::VisualLine ||
         m == VimInputHandler::Mode::VisualBlock;
}

bool isDigit(const QString &s, int *value = nullptr) {
  if (s.size() != 1 || s[0] < u'0' || s[0] > u'9')
    return false;
  if (value)
    *value = s[0].digitValue();
  return true;
}

} // namespace

VimInputHandler::VimInputHandler(QObject *parent) : QObject(parent) {}

VimInputHandler::~VimInputHandler() = default;

// ---------------------------------------------------------------------------------------------
// Entry points

namespace {

// Binds the editor's context for the duration of a call (calls nest: dot-repeat and macros replay keys).
struct Bind {
  Bind(
    EditContext *&ctxRef, InputHost *&hostRef, TextDocument *&docRef, SelectionSet *&selRef,
    EditContext &newCtx, InputHost &newHost
  )
      : ctx(ctxRef), host(hostRef), doc(docRef), sel(selRef), oldCtx(ctxRef), oldHost(hostRef),
        oldDoc(docRef), oldSel(selRef) {
    ctx = &newCtx;
    host = &newHost;
    doc = &newCtx.document;
    sel = &newCtx.selections;
  }
  ~Bind() {
    ctx = oldCtx;
    host = oldHost;
    doc = oldDoc;
    sel = oldSel;
  }
  EditContext *&ctx;
  InputHost *&host;
  TextDocument *&doc;
  SelectionSet *&sel;
  EditContext *oldCtx;
  InputHost *oldHost;
  TextDocument *oldDoc;
  SelectionSet *oldSel;
};

} // namespace

bool VimInputHandler::keyPress(QKeyEvent *event, EditContext &ctx, InputHost &host) {
  const QString symbol = keySymbol(event);
  if (symbol.isEmpty())
    return false;
  Bind bind(m_ctx, m_host, m_doc, m_sel, ctx, host);
  return process(symbol);
}

bool VimInputHandler::feed(const QString &notation, EditContext &ctx, InputHost &host) {
  Bind bind(m_ctx, m_host, m_doc, m_sel, ctx, host);
  bool ok = true;
  for (const QString &symbol : parseKeys(notation))
    ok = process(symbol) && ok;
  return ok;
}

bool VimInputHandler::commitText(const QString &text, EditContext &ctx, InputHost &host) {
  if (m_mode != Mode::Insert && m_mode != Mode::Replace)
    return false;
  Bind bind(m_ctx, m_host, m_doc, m_sel, ctx, host);
  for (qsizetype i = 0; i < text.size();) {
    const qsizetype len = text[i].isHighSurrogate() && i + 1 < text.size() ? 2 : 1;
    process(text.mid(i, len));
    i += len;
  }
  return true;
}

void VimInputHandler::reset() {
  clearPending();
  m_cmdKeys.clear();
  m_cmdChange = false;
  m_insertPending.clear();
  if (m_doc && m_sel)
    closeGroup();
  else
    m_group = false;
}

void VimInputHandler::activate(EditContext &ctx, InputHost &host) {
  Bind bind(m_ctx, m_host, m_doc, m_sel, ctx, host);
  setMode(Mode::Normal);
  m_vis.clear();
  clampCursors();
  highlightSearch();
}

void VimInputHandler::deactivate(EditContext &ctx, InputHost &host) {
  Bind bind(m_ctx, m_host, m_doc, m_sel, ctx, host);
  clearPending();
  if (m_mode == Mode::Insert || m_mode == Mode::Replace)
    finishInsert();
  closeGroup();
  if (isVisualMode(m_mode))
    exitVisual(false);
  setMode(Mode::Normal);
  m_cmdKeys.clear();
  m_host->setSearchHighlight(QRegularExpression());
  // The next handler sees plain cursors; let go of what vim keeps by character.
  m_vis.clear();
  m_doc = nullptr;
  m_sel = nullptr;
}

VimInputHandler::Mode VimInputHandler::mode() const {
  if (!commandLine().isEmpty())
    return Mode::CommandLine;
  if (m_opPending)
    return Mode::OperatorPending;
  return m_mode;
}

QString VimInputHandler::modeName() const {
  switch (mode()) {
  case Mode::Normal:
    return u"NORMAL"_s;
  case Mode::Insert:
    return u"INSERT"_s;
  case Mode::Replace:
    return u"REPLACE"_s;
  case Mode::Visual:
    return u"VISUAL"_s;
  case Mode::VisualLine:
    return u"V-LINE"_s;
  case Mode::VisualBlock:
    return u"V-BLOCK"_s;
  case Mode::OperatorPending:
    return u"OP-PENDING"_s;
  case Mode::CommandLine:
    return u"COMMAND"_s;
  }
  return {};
}

QString VimInputHandler::pendingKeys() const { return formatKeys(m_pending); }

QString VimInputHandler::commandLine() const {
  for (int i = 0; i < m_pending.size(); ++i) {
    const QString &key = m_pending[i];
    if (key == u":"_s || key == u"/"_s || key == u"?"_s)
      return formatKeys(m_pending.mid(i));
  }
  return {};
}

CursorShape VimInputHandler::cursorShape() const {
  switch (m_mode) {
  case Mode::Insert:
    return CursorShape::Line;
  case Mode::Replace:
    return CursorShape::Underline;
  default:
    return CursorShape::Block;
  }
}

bool VimInputHandler::acceptsTextInput() const { return m_mode == Mode::Insert || m_mode == Mode::Replace; }

qsizetype VimInputHandler::cursorOffset(int index, const Selection &selection, const EditContext &ctx) const {
  if (m_mode == Mode::VisualBlock) {
    if (index != ctx.selections.primaryIndex() || m_vis.isEmpty())
      return -1;
    return m_vis.first().cursor;
  }
  if (isVisualMode(m_mode) && index >= 0 && index < m_vis.size() && m_written == ctx.selections.selections())
    return m_vis[index].cursor;
  return selection.head;
}

void VimInputHandler::setIgnoreCase(bool on) {
  if (m_options.ignoreCase == on)
    return;
  m_options.ignoreCase = on;
  m_search.compiled = {};
  emit optionsChanged();
}

void VimInputHandler::setSmartCase(bool on) {
  if (m_options.smartCase == on)
    return;
  m_options.smartCase = on;
  m_search.compiled = {};
  emit optionsChanged();
}

// ---------------------------------------------------------------------------------------------
// Small helpers

const Rope &VimInputHandler::rope() const { return m_doc->rope(); }

int VimInputHandler::tabWidth() const { return qMax(1, m_ctx->settings.tabWidth); }

void VimInputHandler::setMode(Mode mode) {
  if (m_mode == mode)
    return;
  m_mode = mode;
  emit modeChanged();
}

void VimInputHandler::setMessage(const QString &message) {
  if (m_message == message)
    return;
  m_message = message;
  emit messageChanged();
}

void VimInputHandler::setPending(const QStringList &keys) {
  m_pending = keys;
  emit pendingChanged();
  emit modeChanged();
}

void VimInputHandler::clearPending() {
  const bool had = !m_pending.isEmpty() || m_opPending;
  m_pending.clear();
  m_opPending = false;
  m_historyIndex = -1;
  if (had) {
    emit pendingChanged();
    emit modeChanged();
  }
}

QList<qsizetype> VimInputHandler::heads() const {
  QList<qsizetype> out;
  const int n = m_sel->count();
  out.reserve(n);
  for (int i = 0; i < n; ++i)
    out.append(m_sel->at(i).head);
  return out;
}

QList<qsizetype> VimInputHandler::cursorPositions() const {
  if (isVisualMode(m_mode) && !m_vis.isEmpty()) {
    QList<qsizetype> out;
    for (const VisPoint &p : m_vis)
      out.append(p.cursor);
    return out;
  }
  return heads();
}

void VimInputHandler::setCursors(const QList<qsizetype> &cursors, int primary) {
  SelectionList list;
  list.reserve(cursors.size());
  for (qsizetype c : cursors)
    list.append({c, c});
  if (primary < 0)
    primary = m_sel->primaryIndex();
  m_sel->set(list, qBound(0, primary, int(list.size()) - 1));
}

qsizetype VimInputHandler::clampNormal(qsizetype offset) const {
  const Rope &r = rope();
  offset = qBound<qsizetype>(0, offset, r.length());
  const qsizetype line = r.lineAt(offset);
  const qsizetype end = r.lineEnd(line);
  if (offset >= end && end > r.lineStart(line))
    return TextBoundaries(r).previousGrapheme(end);
  return offset;
}

void VimInputHandler::clampCursors() {
  if (m_mode != Mode::Normal && m_mode != Mode::Replace)
    return;
  bool changed = false;
  SelectionList list = m_sel->selections();
  for (Selection &s : list) {
    if (!s.isEmpty())
      continue;
    const qsizetype c = clampNormal(s.head);
    if (c != s.head) {
      s = {c, c};
      changed = true;
    }
  }
  if (changed) {
    m_sel->set(list, m_sel->primaryIndex());
  }
}

void VimInputHandler::ensureGroup() {
  if (m_group || !m_doc)
    return;
  m_doc->beginEditGroup(m_sel->selections());
  m_group = true;
}

void VimInputHandler::closeGroup() {
  if (!m_group || m_groupHold > 0)
    return;
  m_group = false;
  if (m_doc && m_sel)
    m_doc->endEditGroup(m_sel->selections());
}

int VimInputHandler::goalColumn(int index, qsizetype offset) {
  const QList<qsizetype> positions = cursorPositions();
  if (m_goalFor == positions && index < m_goal.size() && m_goal[index] != -2)
    return m_goal[index];
  return virtualColumn(rope(), offset, tabWidth());
}

void VimInputHandler::commitGoals(const QList<int> &goals) {
  m_goal = goals;
  m_goalFor = cursorPositions();
}

QString VimInputHandler::indentUnit() const {
  const EditorSettings &s = m_ctx->settings;
  return s.insertSpaces ? QString(s.indentWidth, u' ') : QStringLiteral("\t");
}

QString VimInputHandler::indentOfLine(qsizetype line, qsizetype upTo) const {
  const Rope &r = rope();
  qsizetype end = firstNonBlank(r, line);
  if (upTo >= 0)
    end = qMin(end, upTo);
  return r.toString(r.lineStart(line), end);
}

// ---------------------------------------------------------------------------------------------
// Processing

bool VimInputHandler::process(const QString &symbol) {
  if (m_depth == 0) {
    if (!m_message.isEmpty())
      setMessage({});
    if (!m_recordReg.isEmpty())
      m_recordKeys.append(symbol);
  }
  if (!m_inDotReplay)
    m_cmdKeys.append(symbol);
  if (m_mode != Mode::Insert && m_mode != Mode::Replace)
    syncExternal();
  if (m_mode == Mode::Insert)
    return processInsert(symbol);
  if (m_mode == Mode::Replace)
    return processReplace(symbol);
  return processNormal(symbol);
}

// The selections can be changed from outside (mouse, host API): a selection in normal mode is
// visual mode, and visual modes follow the selections they find.
void VimInputHandler::syncExternal() {
  if (m_mode == Mode::Normal) {
    for (int i = 0; i < m_sel->count(); ++i) {
      if (!m_sel->at(i).isEmpty()) {
        rebuildVisualFromSelections();
        setMode(Mode::Visual);
        return;
      }
    }
  } else if (isVisualMode(m_mode)) {
    if (m_sel->selections() != m_written) {
      bool anySelected = false;
      for (int i = 0; i < m_sel->count(); ++i)
        anySelected |= !m_sel->at(i).isEmpty();
      rebuildVisualFromSelections();
      if (!anySelected) {
        setMode(Mode::Normal);
        m_vis.clear();
      } else if (m_mode != Mode::Visual) {
        setMode(Mode::Visual);
      }
    }
  }
}

bool VimInputHandler::processNormal(const QString &symbol) {
  const bool visual = isVisualMode(m_mode);

  if (symbol == u"<Esc>"_s || symbol == u"<C-c>"_s) {
    const bool hadPending = !m_pending.isEmpty() || m_opPending;
    clearPending();
    m_cmdKeys.clear();
    m_cmdChange = false;
    if (hadPending)
      return true;
    if (visual) {
      exitVisual();
    } else if (m_sel->count() > 1) {
      m_sel->collapseToPrimary();
    }
    return true;
  }

  // Editing a prompt that is open (":" or a search).
  if (!m_pending.isEmpty()) {
    Cmd probe;
    if (parse(m_pending, probe) == Parse::Incomplete && probe.cmdline >= 0 && exitCmdlineKey(symbol, probe))
      return true;
  }

  // ":" from visual mode starts with the selected lines; the selection ends there.
  if (symbol == u":"_s && visual && m_pending.isEmpty()) {
    const SelectionList sels = m_sel->selections();
    qsizetype first = std::numeric_limits<qsizetype>::max(), last = 0;
    for (const Selection &s : sels) {
      first = qMin(first, s.start());
      last = qMax(last, s.isEmpty() ? s.end() : qMax(s.start(), s.end() - 1));
    }
    setMark(u'<', first);
    setMark(u'>', last);
    m_lastVisual = {m_mode, m_vis, m_toEol, true};
    exitVisual();
    setPending({u":"_s, u"'"_s, u"<"_s, u","_s, u"'"_s, u">"_s});
    return true;
  }

  QStringList keys = m_pending;
  keys.append(symbol);
  Cmd cmd;
  const Parse result = parse(keys, cmd);
  if (result == Parse::Incomplete) {
    setPending(keys);
    const bool wasOp = m_opPending;
    m_opPending = !cmd.op.isEmpty() && cmd.cmdline < 0;
    if (wasOp != m_opPending)
      emit modeChanged();
    return true;
  }
  clearPending();
  if (result == Parse::Invalid) {
    m_cmdKeys.clear();
    m_cmdChange = false;
    return isPrintableSymbol(symbol) || symbol == u"<Tab>"_s || symbol == u"<CR>"_s;
  }

  m_cmdPrefix = cmd.prefixLen;
  m_cmdCount = cmd.count;
  m_cmdReg = cmd.reg;
  m_failed = false;
  execute(cmd);
  if (m_mode != Mode::Insert && m_mode != Mode::Replace) {
    if (m_insertFromNormal && m_mode == Mode::Normal) {
      m_insertFromNormal = false;
      setMode(Mode::Insert);
    } else {
      finishCommand();
    }
  }
  return true;
}

void VimInputHandler::finishCommand() {
  if (m_inDotReplay)
    return;
  if (m_cmdChange && m_cmdKeys.size() >= m_cmdPrefix) {
    Dot dot;
    dot.keys = m_cmdKeys.mid(m_cmdPrefix);
    dot.count = m_cmdCount;
    dot.reg = m_cmdReg;
    dot.valid = true;
    if (m_visDot.valid) {
      dot.visual = true;
      dot.visualMode = m_visDot.visualMode;
      dot.lines = m_visDot.lines;
      dot.columns = m_visDot.columns;
    }
    m_dot = dot;
  }
  m_cmdKeys.clear();
  m_cmdChange = false;
  m_visDot = {};
}

// ---------------------------------------------------------------------------------------------
// Parsing

int VimInputHandler::operatorAt(const QStringList &s, int at, QString *op) const {
  const QString &k = s[at];
  if (k == u"d"_s || k == u"c"_s || k == u"y"_s || k == u">"_s || k == u"<"_s) {
    *op = k;
    return 1;
  }
  if (k == u"g"_s) {
    if (at + 1 >= s.size())
      return 0;
    const QString &second = s[at + 1];
    if (second == u"~"_s || second == u"u"_s || second == u"U"_s) {
      *op = u"g"_s + second;
      return 2;
    }
  }
  return -1;
}

int VimInputHandler::motionLength(const QStringList &s, int at, bool objects) const {
  if (at >= s.size())
    return 0;
  static const QSet<QString> one = {
    u"h"_s,    u"j"_s, u"k"_s,     u"l"_s,      u"w"_s,       u"b"_s,    u"e"_s,      u"W"_s,      u"B"_s,
    u"E"_s,    u"0"_s, u"^"_s,     u"$"_s,      u"G"_s,       u"|"_s,    u"("_s,      u")"_s,      u"{"_s,
    u"}"_s,    u"%"_s, u"H"_s,     u"M"_s,      u"L"_s,       u"-"_s,    u"+"_s,      u"_"_s,      u"<CR>"_s,
    u"<BS>"_s, u" "_s, u"<C-h>"_s, u"<Left>"_s, u"<Right>"_s, u"<Up>"_s, u"<Down>"_s, u"<Home>"_s, u"<End>"_s,
    u";"_s,    u","_s, u"n"_s,     u"N"_s,      u"*"_s,       u"#"_s,    u"<C-n>"_s,  u"<C-p>"_s,
  };
  const QString &k = s[at];
  if (one.contains(k))
    return 1;
  if (k == u"g"_s) {
    if (at + 1 >= s.size())
      return 0;
    static const QSet<QString> second = {u"g"_s, u"e"_s, u"E"_s, u"_"_s, u"j"_s,
                                         u"k"_s, u"0"_s, u"^"_s, u"$"_s};
    return second.contains(s[at + 1]) ? 2 : -1;
  }
  if (k == u"f"_s || k == u"t"_s || k == u"F"_s || k == u"T"_s || k == u"'"_s || k == u"`"_s)
    return at + 1 < s.size() ? 2 : 0;
  if (k == u"/"_s || k == u"?"_s) {
    for (int j = at + 1; j < s.size(); ++j)
      if (s[j] == u"<CR>"_s)
        return j - at + 1;
    return 0;
  }
  if (objects && (k == u"i"_s || k == u"a"_s)) {
    if (at + 1 >= s.size())
      return 0;
    static const QString kinds = QStringLiteral("wWspbB()[]{}<>\"'`t");
    return s[at + 1].size() == 1 && kinds.contains(s[at + 1][0]) ? 2 : -1;
  }
  return -1;
}

int VimInputHandler::commandLength(const QStringList &s, int at) const {
  static const QSet<QString> one = {
    u"x"_s,     u"X"_s,     u"s"_s,     u"S"_s,     u"D"_s,     u"C"_s,        u"Y"_s,          u"p"_s,
    u"P"_s,     u"J"_s,     u"u"_s,     u"U"_s,     u"<C-r>"_s, u"."_s,        u"~"_s,          u"a"_s,
    u"A"_s,     u"i"_s,     u"I"_s,     u"o"_s,     u"O"_s,     u"v"_s,        u"V"_s,          u"<C-v>"_s,
    u"R"_s,     u"&"_s,     u"<C-o>"_s, u"<C-i>"_s, u"<Tab>"_s, u"<C-a>"_s,    u"<C-x>"_s,      u"<C-e>"_s,
    u"<C-y>"_s, u"<C-d>"_s, u"<C-u>"_s, u"<C-f>"_s, u"<C-b>"_s, u"<PageUp>"_s, u"<PageDown>"_s, u"<Del>"_s,
    u"d"_s,     u"c"_s,     u"y"_s,     u">"_s,     u"<"_s,
  };
  const QString &k = s[at];
  if (one.contains(k))
    return 1;
  if (k == u"r"_s || k == u"m"_s || k == u"@"_s)
    return at + 1 < s.size() ? 2 : 0;
  if (k == u"q"_s) {
    if (!m_recordReg.isEmpty())
      return 1;
    return at + 1 < s.size() ? 2 : 0;
  }
  if (k == u"g"_s) {
    if (at + 1 >= s.size())
      return 0;
    static const QSet<QString> second = {u"J"_s, u"v"_s, u"i"_s, u"I"_s, u"p"_s, u"P"_s,
                                         u";"_s, u","_s, u"~"_s, u"u"_s, u"U"_s};
    return second.contains(s[at + 1]) ? 2 : -1;
  }
  if (k == u"z"_s) {
    if (at + 1 >= s.size())
      return 0;
    static const QSet<QString> second = {u"z"_s, u"t"_s, u"b"_s, u"o"_s,    u"c"_s, u"O"_s,
                                         u"C"_s, u"R"_s, u"M"_s, u"<CR>"_s, u"."_s, u"-"_s};
    return second.contains(s[at + 1]) ? 2 : -1;
  }
  if (k == u"Z"_s) {
    if (at + 1 >= s.size())
      return 0;
    return s[at + 1] == u"Z"_s || s[at + 1] == u"Q"_s ? 2 : -1;
  }
  if (k == u"["_s || k == u"]"_s) {
    if (at + 1 >= s.size())
      return 0;
    return s[at + 1] == u"d"_s ? 2 : -1;
  }
  if (k == u":"_s) {
    for (int j = at + 1; j < s.size(); ++j)
      if (s[j] == u"<CR>"_s)
        return j - at + 1;
    return 0;
  }
  return -1;
}

VimInputHandler::Parse VimInputHandler::parse(const QStringList &s, Cmd &cmd) const {
  const int n = s.size();
  const bool visual = isVisualMode(m_mode);
  int i = 0;
  int count = 0;
  QString reg;
  for (;;) {
    if (i >= n)
      return Parse::Incomplete;
    int digit = 0;
    if (s[i] == u"\""_s) {
      if (i + 1 >= n)
        return Parse::Incomplete;
      reg = s[i + 1];
      i += 2;
      continue;
    }
    if (isDigit(s[i], &digit) && (digit != 0 || count > 0)) {
      count = qMin(kMaxCount, count * 10 + digit);
      ++i;
      continue;
    }
    break;
  }
  cmd.count = count;
  cmd.reg = reg;
  cmd.prefixLen = i;

  QString op;
  const int opLength = visual ? -1 : operatorAt(s, i, &op);
  if (opLength == 0)
    return Parse::Incomplete;
  if (opLength > 0) {
    cmd.op = op;
    cmd.type = Cmd::Type::Operator;
    i += opLength;
    int count2 = 0;
    for (;;) {
      if (i >= n)
        return Parse::Incomplete;
      int digit = 0;
      if (isDigit(s[i], &digit) && (digit != 0 || count2 > 0)) {
        count2 = qMin(kMaxCount, count2 * 10 + digit);
        ++i;
        continue;
      }
      break;
    }
    cmd.count2 = count2;
    if (op.size() == 1 && s[i] == op) {
      cmd.doubled = true;
      cmd.keys = {u"<line>"_s};
      return Parse::Complete;
    }
    if (op.size() == 2) {
      if (s[i] == op.mid(1)) {
        cmd.doubled = true;
        cmd.keys = {u"<line>"_s};
        return Parse::Complete;
      }
      if (s[i] == u"g"_s) {
        if (i + 1 >= n)
          return Parse::Incomplete;
        if (s[i + 1] == op.mid(1)) {
          cmd.doubled = true;
          cmd.keys = {u"<line>"_s};
          return Parse::Complete;
        }
      }
    }
    const int length = motionLength(s, i, true);
    if (length == 0) {
      if (s[i] == u"/"_s || s[i] == u"?"_s)
        cmd.cmdline = i;
      return Parse::Incomplete;
    }
    if (length < 0)
      return Parse::Invalid;
    cmd.keys = s.mid(i, length);
    return Parse::Complete;
  }

  const int length = motionLength(s, i, visual);
  if (length > 0) {
    cmd.type = Cmd::Type::Motion;
    cmd.keys = s.mid(i, length);
    return Parse::Complete;
  }
  if (length == 0) {
    if (s[i] == u"/"_s || s[i] == u"?"_s)
      cmd.cmdline = i;
    return Parse::Incomplete;
  }
  const int cl = commandLength(s, i);
  if (cl == 0) {
    if (s[i] == u":"_s)
      cmd.cmdline = i;
    return Parse::Incomplete;
  }
  if (cl < 0)
    return Parse::Invalid;
  cmd.type = Cmd::Type::Command;
  cmd.keys = s.mid(i, cl);
  return Parse::Complete;
}

// Keys inside an open ":" or search prompt that edit it. Returns true when the key was used.
bool VimInputHandler::exitCmdlineKey(const QString &symbol, const Cmd &probe) {
  const int start = probe.cmdline;
  if (symbol == u"<BS>"_s || symbol == u"<C-h>"_s) {
    if (m_pending.size() - 1 <= start) {
      clearPending();
      m_cmdKeys.clear();
    } else {
      QStringList keys = m_pending;
      keys.removeLast();
      setPending(keys);
    }
    return true;
  }
  if (symbol == u"<C-u>"_s) {
    setPending(m_pending.mid(0, start + 1));
    return true;
  }
  if (symbol == u"<C-w>"_s) {
    QStringList keys = m_pending;
    while (keys.size() > start + 1 && keys.last() == u" "_s)
      keys.removeLast();
    while (keys.size() > start + 1 && keys.last() != u" "_s)
      keys.removeLast();
    setPending(keys);
    return true;
  }
  if (symbol == u"<Up>"_s || symbol == u"<Down>"_s) {
    const int which = m_pending[start] == u":"_s ? 0 : 1;
    const QStringList &history = m_history[which];
    if (history.isEmpty())
      return true;
    if (symbol == u"<Up>"_s)
      m_historyIndex = m_historyIndex < 0 ? int(history.size()) - 1 : qMax(0, m_historyIndex - 1);
    else
      m_historyIndex = m_historyIndex < 0 ? -1 : m_historyIndex + 1;
    QStringList keys = m_pending.mid(0, start + 1);
    if (m_historyIndex >= 0 && m_historyIndex < history.size())
      keys += parseKeys(history[m_historyIndex]);
    else
      m_historyIndex = -1;
    setPending(keys);
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------------------------
// Execution

void VimInputHandler::execute(const Cmd &cmd) {
  switch (cmd.type) {
  case Cmd::Type::Motion:
    executeMotion(cmd);
    break;
  case Cmd::Type::Operator:
    executeOperator(cmd);
    break;
  case Cmd::Type::Command:
    if (isVisualMode(m_mode))
      executeVisualCommand(cmd);
    else
      executeCommand(cmd);
    break;
  }
  if (m_mode != Mode::Insert && m_mode != Mode::Replace) {
    closeGroup();
    if (m_mode == Mode::Normal && !m_insertFromNormal)
      clampCursors();
  }
}

void VimInputHandler::executeMotion(const Cmd &cmd) {
  const bool visual = isVisualMode(m_mode);
  const int count = cmd.effective();
  if (visual && cmd.keys.size() == 2 && (cmd.keys[0] == u"i"_s || cmd.keys[0] == u"a"_s)) {
    visualObject(cmd.keys, count);
    return;
  }
  const QList<qsizetype> positions = cursorPositions();
  const Rope &r = rope();
  QList<Target> targets;
  targets.reserve(positions.size());
  bool any = false;
  for (int i = 0; i < positions.size(); ++i) {
    targets.append(evalMotion(cmd.keys, count, positions[i], i, {}, visual));
    any |= targets.last().ok;
  }
  if (!any) {
    m_failed = true;
    return;
  }
  const int primary = m_sel->primaryIndex();
  if (targets[qBound(0, primary, int(targets.size()) - 1)].jump) {
    pushJump(positions[qBound(0, primary, int(positions.size()) - 1)]);
  }
  QList<qsizetype> out = positions;
  QList<int> goals;
  goals.reserve(positions.size());
  bool toEol = false;
  for (int i = 0; i < positions.size(); ++i) {
    const Target &t = targets[i];
    if (!t.ok) { // a failed motion changes nothing, the goal column included
      goals.append(m_goalFor == positions && i < m_goal.size() ? m_goal[i] : -2);
      continue;
    }
    qsizetype to = t.to;
    if (visual) {
      if (t.goal == -1) {
        to = r.lineEnd(r.lineAt(to));
        toEol = true;
      } else {
        const qsizetype end = r.lineEnd(r.lineAt(to));
        if (to >= end && end > r.lineStart(r.lineAt(to)))
          to = TextBoundaries(r).previousGrapheme(end);
      }
    } else {
      to = clampNormal(to);
    }
    out[i] = to;
    // Only vertical motions (and $) set a goal column; any other motion forgets it.
    goals.append(t.goal);
  }
  if (visual) {
    for (int i = 0; i < m_vis.size() && i < out.size(); ++i)
      m_vis[i].cursor = out[i];
    m_toEol = toEol;
    writeVisual();
  } else {
    setCursors(out);
  }
  commitGoals(goals);
  // gj/gk keep their pixel goal in the selection set.
  for (int i = 0; i < targets.size() && i < m_sel->count(); ++i)
    if (targets[i].ok && !std::isnan(targets[i].goalX) && m_sel->count() == targets.size())
      m_sel->setGoalX(i, targets[i].goalX);
}

VimInputHandler::Range VimInputHandler::rangeFor(const Target &t, qsizetype cursor) const {
  const Rope &r = rope();
  TextBoundaries bounds(r);
  Range rg;
  rg.cursor = cursor;
  if (t.object) {
    rg.start = t.from;
    rg.end = t.to;
    rg.linewise = t.linewise;
    rg.cursor = t.from;
    return rg;
  }
  const qsizetype s = qMin(t.from, t.to), e = qMax(t.from, t.to);
  const qsizetype lines = r.lineCount();
  auto lineRange = [&](qsizetype a, qsizetype b) {
    rg.start = r.lineStart(a);
    rg.end = b + 1 < lines ? r.lineStart(b + 1) : r.length();
    rg.linewise = true;
  };
  switch (t.kind) {
  case Kind::Linewise:
    lineRange(r.lineAt(s), r.lineAt(e));
    break;
  case Kind::Inclusive: {
    const qsizetype end = r.lineEnd(r.lineAt(e));
    rg.start = s;
    rg.end = e < end ? bounds.nextGrapheme(e) : e;
    break;
  }
  case Kind::Exclusive: {
    rg.start = s;
    rg.end = e;
    const qsizetype eLine = r.lineAt(e), sLine = r.lineAt(s);
    if (e > s && e == r.lineStart(eLine) && eLine > sLine) {
      if (s <= firstNonBlank(r, sLine)) {
        lineRange(sLine, eLine - 1);
      } else {
        rg.end = r.lineEnd(eLine - 1);
      }
    }
    break;
  }
  }
  if (rg.linewise) {
    const qsizetype headLine = r.lineAt(cursor), startLine = r.lineAt(rg.start);
    if (startLine < headLine) {
      const int col = virtualColumn(r, cursor, tabWidth());
      rg.cursor = offsetAtVirtualColumn(r, startLine, col, tabWidth());
    } else {
      rg.cursor = cursor;
    }
  } else {
    rg.cursor = rg.start;
  }
  return rg;
}

void VimInputHandler::executeOperator(const Cmd &cmd) {
  const int count = cmd.effective();
  const QList<qsizetype> pos = heads();
  const Rope &r = rope();
  QList<Range> ranges;
  bool anyBig = false;
  for (int i = 0; i < pos.size(); ++i) {
    Range rg;
    if (cmd.doubled) {
      const qsizetype line = r.lineAt(pos[i]);
      const qsizetype last = qMin<qsizetype>(line + qMax(1, count) - 1, r.lineCount() - 1);
      rg.start = r.lineStart(line);
      rg.end = last + 1 < r.lineCount() ? r.lineStart(last + 1) : r.length();
      rg.linewise = true;
      rg.cursor = pos[i];
    } else {
      const Target t = evalMotion(cmd.keys, count, pos[i], i, cmd.op, false);
      if (!t.ok)
        continue;
      anyBig |= t.bigMotion;
      rg = rangeFor(t, pos[i]);
    }
    ranges.append(rg);
  }
  if (ranges.isEmpty()) {
    m_failed = true;
    return;
  }
  m_bigMotion = anyBig;
  if (cmd.op != u"y"_s)
    m_cmdChange = true;
  applyOperator(cmd.op, ranges, cmd, false);
}

// ---------------------------------------------------------------------------------------------
// Motions

VimInputHandler::Target VimInputHandler::evalMotion(
  const QStringList &keys, int count, qsizetype off, int index, const QString &op, bool visual
) {
  const Rope &r = rope();
  TextBoundaries bounds(r);
  const int n = qMax(1, count);
  const QString &k = keys.constFirst();
  const qsizetype line = r.lineAt(off);
  const qsizetype lastLine = r.lineCount() - 1;
  const bool operating = !op.isEmpty();
  Target t;
  t.from = off;
  t.to = off;
  auto done = [&](qsizetype to, Kind kind) {
    t.ok = true;
    t.to = to;
    t.kind = kind;
    return t;
  };
  auto lineTarget = [&](qsizetype target, bool firstBlank) {
    t.jump = false;
    return done(firstBlank ? firstNonBlank(r, target) : r.lineStart(target), Kind::Linewise);
  };

  if (k == u"h"_s || k == u"<Left>"_s) {
    qsizetype p = off;
    for (int i = 0; i < n && p > r.lineStart(r.lineAt(p)); ++i)
      p = bounds.previousGrapheme(p);
    if (p == off)
      return t;
    return done(p, Kind::Exclusive);
  }
  if (k == u"<BS>"_s || k == u"<C-h>"_s) {
    qsizetype p = off;
    for (int i = 0; i < n && p > 0; ++i)
      p = bounds.previousGrapheme(p);
    if (p == off)
      return t;
    return done(p, Kind::Exclusive);
  }
  if (k == u"l"_s || k == u"<Right>"_s) {
    qsizetype p = off;
    const qsizetype end = r.lineEnd(line);
    const qsizetype limit = operating || visual ? end : lastCharOffset(r, line);
    for (int i = 0; i < n && p < limit; ++i)
      p = bounds.nextGrapheme(p);
    if (p == off && !operating)
      return t;
    return done(p, Kind::Exclusive);
  }
  if (k == u" "_s) {
    qsizetype p = off;
    for (int i = 0; i < n; ++i) {
      qsizetype l = r.lineAt(p);
      if (p < lastCharOffset(r, l))
        p = bounds.nextGrapheme(p);
      else if (l < lastLine)
        p = r.lineStart(l + 1);
      else if (operating && p < r.lineEnd(l))
        p = r.lineEnd(l);
      else
        break;
    }
    if (p == off)
      return t;
    return done(p, Kind::Exclusive);
  }
  if (
    k == u"j"_s || k == u"<Down>"_s || k == u"<C-n>"_s || k == u"k"_s || k == u"<Up>"_s || k == u"<C-p>"_s
  ) {
    const bool down = k == u"j"_s || k == u"<Down>"_s || k == u"<C-n>"_s;
    const FoldMap *folds = m_ctx->map && m_ctx->map->folds().hasFolds() ? &m_ctx->map->folds() : nullptr;
    qsizetype target = line;
    for (int i = 0; i < n; ++i) {
      qsizetype next;
      if (down) {
        next = folds ? folds->nextVisibleLine(target) : target + 1;
        if (next > lastLine)
          break;
      } else {
        if (target <= 0)
          break;
        next = target - 1;
        if (folds && folds->isHidden(next))
          next = folds->visibleHeaderOf(next);
      }
      target = next;
    }
    if (target == line)
      return t;
    const int goal = goalColumn(index, off);
    const qsizetype to =
      goal < 0 ? lastCharOffset(r, target) : offsetAtVirtualColumn(r, target, goal, tabWidth());
    t.goal = goal;
    return done(to, Kind::Linewise);
  }
  if (k == u"+"_s || k == u"<CR>"_s || k == u"-"_s || k == u"_"_s) {
    qsizetype target = line;
    if (k == u"-"_s)
      target = line - n;
    else if (k == u"_"_s)
      target = line + n - 1;
    else
      target = line + n;
    if (target < 0 || target > lastLine) {
      if (k == u"_"_s)
        target = qMin(target, lastLine);
      else
        return t;
    }
    return lineTarget(target, true);
  }
  if (k == u"G"_s) {
    const qsizetype target = count > 0 ? qMin<qsizetype>(count - 1, lastLine) : lastLine;
    lineTarget(target, true);
    t.jump = true;
    return t;
  }
  if (k == u"g"_s && keys[1] == u"g"_s) {
    const qsizetype target = count > 0 ? qMin<qsizetype>(count - 1, lastLine) : 0;
    lineTarget(target, true);
    t.jump = true;
    return t;
  }
  if (k == u"|"_s) {
    t.goal = n - 1;
    return done(offsetAtVirtualColumn(r, line, n - 1, tabWidth()), Kind::Exclusive);
  }
  if (k == u"0"_s || k == u"<Home>"_s) {
    t.goal = 0;
    return done(r.lineStart(line), Kind::Exclusive);
  }
  if (k == u"^"_s) {
    t.goal = -2;
    return done(firstNonBlank(r, line), Kind::Exclusive);
  }
  if (k == u"$"_s || k == u"<End>"_s) {
    const qsizetype target = qMin<qsizetype>(line + n - 1, lastLine);
    if (n > 1 && line + n - 1 > lastLine && operating)
      return t;
    t.goal = -1;
    return done(lastCharOffset(r, target), Kind::Inclusive);
  }
  if (k == u"g"_s && keys[1] == u"_"_s) {
    const qsizetype target = qMin<qsizetype>(line + n - 1, lastLine);
    return done(lastNonBlank(r, target), Kind::Inclusive);
  }
  if (k == u"w"_s || k == u"W"_s) {
    const bool big = k == u"W"_s;
    if (op == u"c"_s) {
      const CharClass cls = classAt(r, off, big);
      if (cls == CharClass::Word || cls == CharClass::Punct) {
        // "cw" on a word changes up to its end, not the blanks after it.
        qsizetype p = off;
        const qsizetype end = r.lineEnd(line);
        for (qsizetype q = bounds.nextGrapheme(p); q < end && classAt(r, q, big) == cls;
             q = bounds.nextGrapheme(q))
          p = q;
        for (int i = 1; i < n; ++i)
          p = wordEnd(r, p, 1, big);
        return done(p, Kind::Inclusive);
      }
    }
    qsizetype p = wordForward(r, off, n - (operating ? 1 : 0), big);
    if (operating) {
      // The last word moved over ends the operated text at the end of its line instead of
      // taking the first word of the next one ("dw" on the last word of a line).
      const qsizetype before = p;
      const qsizetype next = wordForward(r, p, 1, big);
      const qsizetype pLine = r.lineAt(p);
      if (next > before && r.lineAt(next) > pLine && p < r.lineEnd(pLine))
        p = r.lineEnd(pLine);
      else
        p = next;
    }
    if (p == off)
      return operating ? done(p, Kind::Exclusive) : t;
    return done(p, Kind::Exclusive);
  }
  if (k == u"b"_s || k == u"B"_s) {
    if (off == 0)
      return t;
    return done(wordBackward(r, off, n, k == u"B"_s), Kind::Exclusive);
  }
  if (k == u"e"_s || k == u"E"_s) {
    const qsizetype p = wordEnd(r, off, n, k == u"E"_s);
    if (p == off && !operating)
      return t;
    return done(p, Kind::Inclusive);
  }
  if (k == u"g"_s && (keys[1] == u"e"_s || keys[1] == u"E"_s)) {
    if (off == 0)
      return t;
    return done(wordEndBackward(r, off, n, keys[1] == u"E"_s), Kind::Inclusive);
  }
  if (k == u"f"_s || k == u"t"_s || k == u"F"_s || k == u"T"_s) {
    m_lastFind = keys[1];
    m_lastFindForward = k == u"f"_s || k == u"t"_s;
    m_lastFindTill = k == u"t"_s || k == u"T"_s;
    const qsizetype p = findCharInLine(r, off, keys[1], n, m_lastFindForward, m_lastFindTill);
    if (p < 0)
      return t;
    return done(p, m_lastFindForward ? Kind::Inclusive : Kind::Exclusive);
  }
  if (k == u";"_s || k == u","_s) {
    if (m_lastFind.isEmpty())
      return t;
    const bool forward = k == u";"_s ? m_lastFindForward : !m_lastFindForward;
    const qsizetype p = findCharInLine(r, off, m_lastFind, n, forward, m_lastFindTill, true);
    if (p < 0)
      return t;
    return done(p, forward ? Kind::Inclusive : Kind::Exclusive);
  }
  if (k == u"%"_s) {
    if (count > 0) {
      const qsizetype target =
        qBound<qsizetype>(0, (qsizetype(count) * r.lineCount() + 99) / 100 - 1, lastLine);
      lineTarget(target, true);
      t.jump = true;
      return t;
    }
    static const BracketPairs pairs = {{u'(', u')'}, {u'[', u']'}, {u'{', u'}'}};
    const qsizetype end = r.lineEnd(line);
    qsizetype bracket = -1;
    for (qsizetype p = off; p < end; ++p) {
      const QChar c = r.at(p);
      if (c == u'(' || c == u')' || c == u'[' || c == u']' || c == u'{' || c == u'}') {
        bracket = p;
        break;
      }
    }
    if (bracket < 0)
      return t;
    const BracketPair pair = findMatchingBracket(r, bracket, pairs);
    if (!pair.valid())
      return t;
    t.jump = true;
    return done(bracket == pair.open ? pair.close : pair.open, Kind::Inclusive);
  }
  if (k == u"}"_s || k == u"{"_s) {
    t.jump = true;
    const qsizetype p = k == u"}"_s ? paragraphForward(r, off, n) : paragraphBackward(r, off, n);
    if (p == off)
      return t.ok = false, t;
    return done(p, Kind::Exclusive);
  }
  if (k == u")"_s || k == u"("_s) {
    t.jump = true;
    const qsizetype p = k == u")"_s ? sentenceForward(r, off, n) : sentenceBackward(r, off, n);
    if (p == off)
      return t.ok = false, t;
    return done(p, Kind::Exclusive);
  }
  if (k == u"H"_s || k == u"M"_s || k == u"L"_s) {
    const InputHost::VisibleRows rows = m_host->visibleRows();
    if (!rows.valid || !m_ctx->map)
      return t;
    qsizetype row;
    if (k == u"H"_s)
      row = rows.first + n - 1;
    else if (k == u"L"_s)
      row = rows.last - (n - 1);
    else
      row = (rows.first + qMin(rows.last, m_ctx->map->rowCount() - 1)) / 2;
    row = qBound<qsizetype>(rows.first, row, qMin(rows.last, m_ctx->map->rowCount() - 1));
    lineTarget(m_ctx->map->rowAt(row).line, true);
    t.jump = true;
    return t;
  }
  if (
    k == u"g"_s &&
    (keys[1] == u"j"_s || keys[1] == u"k"_s || keys[1] == u"0"_s || keys[1] == u"^"_s || keys[1] == u"$"_s)
  ) {
    if (!m_ctx->map || !m_ctx->layout)
      return t;
    const DisplayMap &map = *m_ctx->map;
    const qsizetype row = map.rowForPosition(r.positionAt(off));
    if (keys[1] == u"0"_s || keys[1] == u"^"_s || keys[1] == u"$"_s) {
      const DisplayRow dr = map.rowAt(row);
      const qsizetype base = r.lineStart(dr.line);
      if (keys[1] == u"$"_s)
        return done(r.snapToCodePoint(base + dr.lastCursorColumn()), Kind::Inclusive);
      qsizetype p = base + dr.startColumn;
      if (keys[1] == u"^"_s)
        while (p < r.lineEnd(dr.line) && (r.at(p) == u' ' || r.at(p) == u'\t'))
          ++p;
      return done(p, Kind::Exclusive);
    }
    const bool down = keys[1] == u"j"_s;
    const qsizetype target = row + (down ? n : -n);
    if (target < 0 || target >= map.rowCount()) {
      if ((down && row >= map.rowCount() - 1) || (!down && row == 0))
        return t;
    }
    const qsizetype clamped = qBound<qsizetype>(0, target, map.rowCount() - 1);
    qreal x = index < m_sel->count() ? m_sel->goalX(index) : SelectionSet::NoGoal;
    if (std::isnan(x))
      x = m_ctx->layout->xForOffset(off);
    t.goalX = x;
    return done(m_ctx->layout->offsetForX(map.rowAt(clamped), x), Kind::Exclusive);
  }
  if (k == u"'"_s || k == u"`"_s) {
    const QChar name = keys[1].isEmpty() ? QChar() : keys[1][0];
    const qsizetype at = markOffset(name);
    if (at < 0) {
      setMessage(u"E20: Mark not set"_s);
      return t;
    }
    t.jump = name != u'<' && name != u'>' && name != u'[' && name != u']';
    if (k == u"'"_s)
      return lineTarget(r.lineAt(at), true), t.jump = true, t;
    return done(at, Kind::Exclusive);
  }
  if (k == u"/"_s || k == u"?"_s || k == u"n"_s || k == u"N"_s || k == u"*"_s || k == u"#"_s) {
    t = evalSearch(keys, count, off, k == u"N"_s, k == u"?"_s || k == u"#"_s, k == u"n"_s || k == u"N"_s);
    t.from = off;
    return t;
  }
  if ((k == u"i"_s || k == u"a"_s) && keys.size() == 2) {
    const ObjectRange o = textObject(r, off, keys[1][0], k == u"a"_s, n);
    if (!o.ok)
      return t;
    t.ok = true;
    t.object = true;
    t.from = o.start;
    t.to = o.end;
    t.linewise = o.linewise;
    return t;
  }
  return t;
}

// ---------------------------------------------------------------------------------------------
// Commands

void VimInputHandler::executeCommand(const Cmd &cmd) {
  const QString &k = cmd.keys.constFirst();
  const int count = cmd.effective();
  const int n = qMax(1, count);
  const Rope &r = rope();
  const QChar regName = cmd.reg.isEmpty() ? QChar() : cmd.reg[0];

  auto synth = [&](const QString &op, const QStringList &motion, int cnt) {
    Cmd c;
    c.type = Cmd::Type::Operator;
    c.op = op;
    c.keys = motion;
    c.doubled = motion == QStringList{u"<line>"_s};
    c.count = cnt;
    c.reg = cmd.reg;
    executeOperator(c);
  };
  auto moveCursors = [&](auto fn) {
    QList<qsizetype> out = heads();
    for (qsizetype &o : out)
      o = fn(o);
    setCursors(out);
  };

  if (k == u"x"_s || k == u"<Del>"_s) {
    synth(u"d"_s, {u"l"_s}, count);
  } else if (k == u"X"_s) {
    synth(u"d"_s, {u"h"_s}, count);
  } else if (k == u"s"_s) {
    synth(u"c"_s, {u"l"_s}, count);
  } else if (k == u"S"_s) {
    synth(u"c"_s, {u"<line>"_s}, count);
  } else if (k == u"D"_s) {
    synth(u"d"_s, {u"$"_s}, count);
  } else if (k == u"C"_s) {
    synth(u"c"_s, {u"$"_s}, count);
  } else if (k == u"Y"_s) {
    synth(u"y"_s, {u"<line>"_s}, count);
  } else if (k == u"p"_s || k == u"P"_s) {
    m_cmdChange = true;
    pasteRegister(regName, k == u"p"_s, n);
  } else if (k == u"g"_s && (cmd.keys[1] == u"p"_s || cmd.keys[1] == u"P"_s)) {
    m_cmdChange = true;
    pasteRegister(regName, cmd.keys[1] == u"p"_s, n, true);
  } else if (k == u"J"_s) {
    m_cmdChange = true;
    joinLines(qMax(2, n), true);
  } else if (k == u"g"_s && cmd.keys[1] == u"J"_s) {
    m_cmdChange = true;
    joinLines(qMax(2, n), false);
  } else if (k == u"u"_s || k == u"<C-r>"_s) {
    closeGroup();
    bool any = false;
    for (int i = 0; i < n; ++i) {
      const bool ok = k == u"u"_s ? commands::undo(*m_ctx) : commands::redo(*m_ctx);
      if (!ok)
        break;
      any = true;
    }
    if (any)
      afterUndo();
    else
      setMessage(k == u"u"_s ? u"Already at oldest change"_s : u"Already at newest change"_s);
  } else if (k == u"."_s) {
    replayDot(count);
  } else if (k == u"~"_s) {
    m_cmdChange = true;
    toggleCaseAtCursor(n);
  } else if (k == u"a"_s) {
    if (m_ctx->settings.readOnly)
      return;
    moveCursors([&](qsizetype o) {
      return o < r.lineEnd(r.lineAt(o)) ? TextBoundaries(r).nextGrapheme(o) : o;
    });
    startInsert(n);
  } else if (k == u"A"_s) {
    if (m_ctx->settings.readOnly)
      return;
    moveCursors([&](qsizetype o) { return r.lineEnd(r.lineAt(o)); });
    startInsert(n);
  } else if (k == u"i"_s) {
    startInsert(n);
  } else if (k == u"I"_s) {
    moveCursors([&](qsizetype o) { return firstNonBlank(r, r.lineAt(o)); });
    startInsert(n);
  } else if (k == u"g"_s && cmd.keys[1] == u"I"_s) {
    moveCursors([&](qsizetype o) { return r.lineStart(r.lineAt(o)); });
    startInsert(n);
  } else if (k == u"g"_s && cmd.keys[1] == u"i"_s) {
    const qsizetype at = markOffset(u'^');
    if (at >= 0)
      setCursors({at});
    startInsert(n);
  } else if (k == u"o"_s || k == u"O"_s) {
    if (m_ctx->settings.readOnly)
      return;
    openLine(k == u"o"_s, n);
  } else if (k == u"R"_s) {
    startReplace(n);
  } else if (k == u"v"_s) {
    enterVisual(Mode::Visual);
  } else if (k == u"V"_s) {
    enterVisual(Mode::VisualLine);
  } else if (k == u"<C-v>"_s) {
    enterVisual(Mode::VisualBlock);
  } else if (k == u"g"_s && cmd.keys[1] == u"v"_s) {
    reselectLastVisual();
  } else if (k == u":"_s) {
    QString line;
    for (int i = 1; i < cmd.keys.size() - 1; ++i)
      line += cmd.keys[i];
    executeEx(line);
  } else if (k == u"r"_s) {
    m_cmdChange = true;
    replaceChars(cmd.keys[1], n);
  } else if (k == u"m"_s) {
    if (!cmd.keys[1].isEmpty())
      setMark(cmd.keys[1][0], m_sel->primary().head);
  } else if (k == u"q"_s) {
    if (!m_recordReg.isEmpty()) {
      if (!m_recordKeys.isEmpty())
        m_recordKeys.removeLast(); // the q that ends it
      const QString name = m_recordReg;
      m_recordReg.clear();
      QString text = formatKeys(m_recordKeys);
      m_recordKeys.clear();
      Register existing = fetchRegister(name[0]);
      const bool append = name[0].isUpper();
      if (append && !existing.isEmpty())
        text = existing.text + text;
      Register reg;
      reg.text = text;
      reg.pieces = {text};
      m_regs[name[0].toLower()] = reg;
      m_regs[u'"'] = reg;
      emit recordingChanged();
    } else if (cmd.keys[1].size() == 1 && (cmd.keys[1][0].isLetterOrNumber() || cmd.keys[1] == u"\""_s)) {
      m_recordReg = cmd.keys[1];
      m_recordKeys.clear();
      emit recordingChanged();
    }
  } else if (k == u"@"_s) {
    runMacro(cmd.keys[1].isEmpty() ? QChar() : cmd.keys[1][0], n);
  } else if (k == u"<C-o>"_s) {
    jumpOlder(n, true);
  } else if (k == u"<C-i>"_s || k == u"<Tab>"_s) {
    jumpOlder(n, false);
  } else if (k == u"<C-a>"_s || k == u"<C-x>"_s) {
    m_cmdChange = true;
    incrementNumber(k == u"<C-a>"_s ? n : -n);
  } else if (
    k == u"<C-e>"_s || k == u"<C-y>"_s || k == u"<C-d>"_s || k == u"<C-u>"_s || k == u"<C-f>"_s ||
    k == u"<C-b>"_s || k == u"<PageUp>"_s || k == u"<PageDown>"_s
  ) {
    if (!m_ctx->map || !m_ctx->layout)
      return;
    const qsizetype page = qMax<qsizetype>(1, m_ctx->layout->pageRows());
    qsizetype rows = 0;
    bool moveCursor = true;
    if (k == u"<C-e>"_s || k == u"<C-y>"_s) {
      rows = k == u"<C-e>"_s ? n : -n;
      moveCursor = false;
    } else if (k == u"<C-d>"_s || k == u"<C-u>"_s) {
      rows = qMax<qsizetype>(1, page / 2) * (k == u"<C-d>"_s ? 1 : -1);
    } else {
      rows = page * n * (k == u"<C-f>"_s || k == u"<PageDown>"_s ? 1 : -1);
    }
    const DisplayMap &map = *m_ctx->map;
    m_host->scrollRows(rows);
    const InputHost::VisibleRows visible = m_host->visibleRows();
    QList<qsizetype> out = heads();
    for (qsizetype &o : out) {
      qsizetype row = map.rowForPosition(r.positionAt(o));
      if (moveCursor)
        row += rows;
      else if (visible.valid)
        row = qBound(visible.first, row, visible.last);
      row = qBound<qsizetype>(0, row, map.rowCount() - 1);
      o =
        moveCursor || row != map.rowForPosition(r.positionAt(o)) ? firstNonBlank(r, map.rowAt(row).line) : o;
    }
    setCursors(out);
  } else if (k == u"z"_s) {
    const QString &z = cmd.keys[1];
    if (z == u"o"_s || z == u"O"_s) {
      m_host->foldCommand(FoldCommand::UnfoldAtCursor);
    } else if (z == u"c"_s || z == u"C"_s) {
      m_host->foldCommand(FoldCommand::FoldAtCursor);
    } else if (z == u"R"_s) {
      m_host->foldCommand(FoldCommand::UnfoldAll);
    } else if (z == u"M"_s) {
      m_host->foldCommand(FoldCommand::FoldAll);
    } else if (m_ctx->map) {
      const InputHost::VisibleRows visible = m_host->visibleRows();
      if (!visible.valid)
        return;
      const qsizetype row = m_ctx->map->rowForPosition(r.positionAt(m_sel->primary().head));
      qsizetype target = (visible.first + visible.last) / 2;
      if (z == u"t"_s || z == u"<CR>"_s)
        target = visible.first;
      else if (z == u"b"_s || z == u"-"_s)
        target = visible.last;
      m_host->scrollRows(row - target);
      if (z == u"<CR>"_s || z == u"."_s || z == u"-"_s)
        moveCursors([&](qsizetype o) { return firstNonBlank(r, r.lineAt(o)); });
    }
  } else if (k == u"Z"_s) {
    if (cmd.keys[1] == u"Z"_s) {
      emit writeRequested(QString());
      emit quitRequested(false);
    } else {
      emit quitRequested(true);
    }
  } else if (k == u"["_s || k == u"]"_s) {
    m_host->gotoDiagnostic(k == u"]"_s);
  }
}

void VimInputHandler::replayKeys(const QStringList &keys) {
  if (m_depth >= kMaxReplayDepth)
    return;
  ++m_depth;
  for (const QString &key : keys) {
    process(key);
    if (m_failed)
      break;
  }
  --m_depth;
}

void VimInputHandler::replayDot(int count) {
  if (!m_dot.valid || m_depth >= kMaxReplayDepth)
    return;
  QStringList keys;
  QString reg = m_dot.reg;
  if (!reg.isEmpty()) {
    keys += {u"\""_s, reg};
    if (reg.size() == 1 && reg[0] >= u'1' && reg[0] < u'9')
      m_dot.reg = QString(QChar(reg[0].unicode() + 1)); // "1p... . . steps through the numbered registers
  }
  const int c = count > 0 ? count : m_dot.count;
  if (c > 0)
    for (QChar digit : QString::number(c))
      keys.append(QString(digit));
  keys += m_dot.keys;
  if (count > 0)
    m_dot.count = count;

  if (m_dot.visual) {
    // Reselect a region of the same size at the cursor, then run the operator on it.
    const Rope &r = rope();
    QList<VisPoint> points;
    for (qsizetype head : heads()) {
      VisPoint p{head, head};
      const qsizetype line = r.lineAt(head);
      const qsizetype lastLine = qMin<qsizetype>(r.lineCount() - 1, line + m_dot.lines - 1);
      if (m_dot.visualMode == Mode::VisualLine) {
        p.cursor = r.lineStart(lastLine);
      } else if (m_dot.visualMode == Mode::VisualBlock) {
        const int col = virtualColumn(r, head, tabWidth());
        p.cursor = offsetAtVirtualColumn(r, lastLine, col + int(m_dot.columns) - 1, tabWidth());
        if (p.cursor >= r.lineEnd(lastLine) && r.lineLength(lastLine) > 0)
          p.cursor = lastCharOffset(r, lastLine);
      } else if (m_dot.lines <= 1) {
        p.cursor = qMin(head + m_dot.columns - 1, lastCharOffset(r, line));
      } else {
        p.cursor = qMin(r.lineStart(lastLine) + m_dot.columns - 1, lastCharOffset(r, lastLine));
      }
      points.append(p);
      if (m_dot.visualMode == Mode::VisualBlock)
        break;
    }
    m_vis = points;
    m_toEol = false;
    setMode(m_dot.visualMode);
    writeVisual();
  }

  m_inDotReplay = true;
  ++m_groupHold;
  replayKeys(keys);
  --m_groupHold;
  m_inDotReplay = false;
  m_cmdChange = false;
  m_visDot = {};
  m_failed = false;
}

void VimInputHandler::runMacro(QChar name, int count) {
  if (name.isNull())
    return;
  if (name == u'@')
    name = m_lastMacro.isEmpty() ? QChar() : m_lastMacro[0];
  if (name == u':') {
    if (!m_lastEx.isEmpty())
      executeEx(m_lastEx);
    return;
  }
  if (name.isNull())
    return;
  const Register reg = fetchRegister(name);
  if (reg.isEmpty())
    return;
  m_lastMacro = QString(name);
  const QStringList keys = parseKeys(reg.text);
  m_cmdKeys.clear();
  m_cmdChange = false;
  ++m_groupHold;
  for (int i = 0; i < count && !m_failed; ++i)
    replayKeys(keys);
  --m_groupHold;
  // Whatever the macro left open (an unfinished insert, a half-typed command) ends with it.
  if (m_mode == Mode::Insert || m_mode == Mode::Replace)
    finishInsert();
  clearPending();
  m_cmdKeys.clear();
  m_cmdChange = false;
  closeGroup();
}

void VimInputHandler::afterUndo() {
  SelectionList list = m_sel->selections();
  bool changed = false;
  for (Selection &s : list) {
    if (!s.isEmpty()) {
      s = {s.start(), s.start()};
      changed = true;
    }
  }
  if (changed)
    m_sel->set(list, m_sel->primaryIndex());
  if (isVisualMode(m_mode)) {
    m_vis.clear();
    setMode(Mode::Normal);
  }
  clampCursors();
}

// ---------------------------------------------------------------------------------------------
// Insert and replace mode

void VimInputHandler::startInsert(int count, bool openedLines) {
  if (m_ctx->settings.readOnly || m_doc->isLoading())
    return;
  m_insertKeys.clear();
  m_insertCount = qMax(1, count);
  m_insertOpened = openedLines;
  m_blockInsert = false;
  m_cmdChange = true;
  ensureGroup();
  setMode(Mode::Insert);
}

void VimInputHandler::startReplace(int count) {
  if (m_ctx->settings.readOnly || m_doc->isLoading())
    return;
  m_insertKeys.clear();
  m_insertCount = qMax(1, count);
  m_insertOpened = false;
  m_blockInsert = false;
  m_replaceStack.clear();
  m_cmdChange = true;
  ensureGroup();
  setMode(Mode::Replace);
}

void VimInputHandler::repeatInsert(int times) {
  const QStringList keys = m_insertKeys;
  m_inRepeat = true;
  for (int t = 0; t < times; ++t) {
    if (m_insertOpened)
      openLine(true, 0);
    for (const QString &key : keys) {
      if (m_mode == Mode::Replace)
        processReplace(key);
      else
        processInsert(key);
    }
  }
  m_inRepeat = false;
}

void VimInputHandler::finishInsert() {
  if (m_insertCount > 1 && !m_inRepeat)
    repeatInsert(m_insertCount - 1);
  m_insertCount = 1;
  QString typed;
  for (const QString &key : m_insertKeys)
    if (isPrintableSymbol(key))
      typed += key;
    else if (key == u"<CR>"_s)
      typed += u'\n';
  m_lastInserted = typed;
  m_insertPending.clear();
  m_replaceStack.clear();
  const bool block = m_blockInsert;
  m_blockInsert = false;
  setMode(Mode::Normal);
  const Rope &r = rope();
  TextBoundaries bounds(r);
  SelectionList list = m_sel->selections();
  QList<qsizetype> out;
  for (const Selection &s : list) {
    qsizetype head = s.head;
    if (head > r.lineStart(r.lineAt(head)))
      head = bounds.previousGrapheme(head);
    out.append(head);
  }
  int primary = m_sel->primaryIndex();
  if (block && !out.isEmpty()) {
    out = {clampNormal(qMin(m_blockOrigin, r.length()))};
    primary = 0;
  }
  if (!out.isEmpty()) {
    setMark(u'^', list[qBound(0, primary, int(list.size()) - 1)].head);
    setCursors(out, primary);
    setMark(u'.', out[qBound(0, primary, int(out.size()) - 1)]);
  }
  closeGroup();
  clampCursors();
  if (!m_inRepeat)
    finishCommand();
}

bool VimInputHandler::processInsert(const QString &symbol) {
  if (!m_insertPending.isEmpty()) {
    const QString pending = m_insertPending.constFirst();
    m_insertPending.clear();
    if (!m_inRepeat)
      m_insertKeys.append(symbol);
    ensureGroup();
    if (pending == u"<C-r>"_s) {
      if (symbol.size() == 1) {
        const Register reg = fetchRegister(symbol[0]);
        if (!reg.isEmpty()) {
          QString text = reg.text;
          commands::insertText(*m_ctx, text);
        }
      }
    } else if (isPrintableSymbol(symbol)) { // <C-v>: the next key literally
      commands::insertText(*m_ctx, symbol);
    } else if (symbol == u"<Tab>"_s) {
      commands::insertText(*m_ctx, u"\t"_s);
    }
    return true;
  }
  if (symbol == u"<Esc>"_s || symbol == u"<C-c>"_s) {
    finishInsert();
    return true;
  }
  ensureGroup();
  if (!m_inRepeat)
    m_insertKeys.append(symbol);
  return processInsertKey(symbol);
}

bool VimInputHandler::processInsertKey(const QString &symbol) {
  EditContext &ctx = *m_ctx;
  using namespace commands;
  if (symbol == u"<BS>"_s || symbol == u"<C-h>"_s) {
    deleteBackward(ctx);
  } else if (symbol == u"<Del>"_s) {
    deleteForward(ctx);
  } else if (symbol == u"<CR>"_s || symbol == u"<C-m>"_s || symbol == u"<C-j>"_s) {
    newline(ctx);
  } else if (symbol == u"<Tab>"_s) {
    indent(ctx);
  } else if (symbol == u"<S-Tab>"_s || symbol == u"<C-d>"_s) {
    outdent(ctx);
  } else if (symbol == u"<C-w>"_s) {
    deleteWordBackward(ctx);
  } else if (symbol == u"<C-u>"_s) {
    const Rope &r = rope();
    QList<Edit> edits;
    qsizetype previousEnd = 0;
    for (int i = 0; i < m_sel->count(); ++i) {
      const qsizetype head = m_sel->at(i).head;
      const qsizetype line = r.lineAt(head);
      qsizetype start = r.lineStart(line);
      const qsizetype indentEnd = firstNonBlank(r, line);
      if (head > indentEnd)
        start = indentEnd;
      start = qMax(start, previousEnd);
      edits.append({start, head, {}});
      previousEnd = head;
    }
    edit(edits);
  } else if (symbol == u"<C-r>"_s) {
    m_insertPending = {symbol};
  } else if (symbol == u"<C-v>"_s || symbol == u"<C-q>"_s) {
    m_insertPending = {u"<C-v>"_s};
  } else if (symbol == u"<C-a>"_s) {
    if (!m_lastInserted.isEmpty())
      insertText(ctx, m_lastInserted);
  } else if (symbol == u"<C-o>"_s) {
    closeGroup();
    m_insertFromNormal = true;
    setMode(Mode::Normal);
  } else if (symbol == u"<Left>"_s) {
    move(ctx, Movement::CharLeft);
  } else if (symbol == u"<Right>"_s) {
    move(ctx, Movement::CharRight);
  } else if (symbol == u"<Up>"_s) {
    move(ctx, Movement::RowUp);
  } else if (symbol == u"<Down>"_s) {
    move(ctx, Movement::RowDown);
  } else if (symbol == u"<Home>"_s) {
    move(ctx, Movement::RowStart);
  } else if (symbol == u"<End>"_s) {
    move(ctx, Movement::RowEnd);
  } else if (symbol == u"<PageUp>"_s) {
    move(ctx, Movement::PageUp);
  } else if (symbol == u"<PageDown>"_s) {
    move(ctx, Movement::PageDown);
  } else if (isPrintableSymbol(symbol)) {
    typeText(ctx, symbol);
  } else {
    return false;
  }
  return true;
}

bool VimInputHandler::processReplace(const QString &symbol) {
  if (symbol == u"<Esc>"_s || symbol == u"<C-c>"_s) {
    finishInsert();
    return true;
  }
  ensureGroup();
  if (!m_inRepeat)
    m_insertKeys.append(symbol);
  const Rope &r = rope();
  TextBoundaries bounds(r);
  if (symbol == u"<BS>"_s) {
    if (m_replaceStack.isEmpty()) {
      commands::move(*m_ctx, Movement::CharLeft);
      return true;
    }
    const ReplaceStep step = m_replaceStack.takeLast();
    QList<Edit> edits;
    const int n = m_sel->count();
    for (int i = 0; i < n && i < step.typed.size(); ++i) {
      const qsizetype head = m_sel->at(i).head;
      edits.append({head - step.typed[i].size(), head, step.removed[i]});
    }
    const EditResult res = edit(edits);
    if (res.ok)
      setCursors(res.starts);
    return true;
  }
  if (isPrintableSymbol(symbol)) {
    QList<Edit> edits;
    ReplaceStep step;
    const int n = m_sel->count();
    qsizetype previousEnd = 0;
    for (int i = 0; i < n; ++i) {
      const qsizetype head = m_sel->at(i).head;
      const qsizetype end = head < r.lineEnd(r.lineAt(head)) ? bounds.nextGrapheme(head) : head;
      const qsizetype start = qMax(head, previousEnd);
      edits.append({start, qMax(start, end), symbol});
      step.removed.append(r.toString(start, qMax(start, end)));
      step.typed.append(symbol);
      previousEnd = qMax(start, end);
    }
    const EditResult res = edit(edits);
    if (res.ok) {
      setCursors(res.ends);
      m_replaceStack.append(step);
    }
    return true;
  }
  if (symbol == u"<CR>"_s) {
    commands::newline(*m_ctx);
    m_replaceStack.clear();
    return true;
  }
  return processInsertKey(symbol);
}

void VimInputHandler::openLine(bool below, int count) {
  const Rope &r = rope();
  const FoldMap *folds = m_ctx->map && m_ctx->map->folds().hasFolds() ? &m_ctx->map->folds() : nullptr;
  QList<Edit> edits;
  QList<qsizetype> lines;
  for (qsizetype head : heads()) {
    qsizetype line = r.lineAt(head);
    if (!lines.isEmpty() && lines.last() == line)
      continue;
    lines.append(line);
  }
  QList<qsizetype> inner; // indent length per edit
  for (qsizetype line : lines) {
    const QString indentText = indentOfLine(line);
    if (below) {
      const qsizetype last = folds ? folds->nextVisibleLine(line) - 1 : line;
      edits.append({r.lineEnd(last), r.lineEnd(last), u"\n"_s + indentText});
    } else {
      edits.append({r.lineStart(line), r.lineStart(line), indentText + u"\n"_s});
    }
    inner.append(indentText.size());
  }
  const EditResult res = edit(edits);
  if (!res.ok)
    return;
  QList<qsizetype> cursors;
  for (int i = 0; i < res.starts.size(); ++i)
    cursors.append(below ? res.ends[i] : res.starts[i] + inner[i]);
  setCursors(cursors, qMin(m_sel->primaryIndex(), int(cursors.size()) - 1));
  if (count > 0)
    startInsert(count, true);
}

// ---------------------------------------------------------------------------------------------
// Visual mode

Selection VimInputHandler::charSelection(const VisPoint &p) const {
  const Rope &r = rope();
  TextBoundaries bounds(r);
  if (p.cursor >= p.anchor) {
    const qsizetype end = p.cursor < r.length() ? bounds.nextGrapheme(p.cursor) : p.cursor;
    return {p.anchor, end};
  }
  const qsizetype anchorEnd = p.anchor < r.length() ? bounds.nextGrapheme(p.anchor) : p.anchor;
  return {anchorEnd, p.cursor};
}

SelectionList VimInputHandler::blockSelections(const VisPoint &p, int *primary) const {
  const Rope &r = rope();
  TextBoundaries bounds(r);
  const int tw = tabWidth();
  const qsizetype la = r.lineAt(p.anchor), lc = r.lineAt(p.cursor);
  const qsizetype first = qMin(la, lc), last = qMax(la, lc);
  const int va = virtualColumn(r, p.anchor, tw), vc = virtualColumn(r, p.cursor, tw);
  const int wa = cellWidthAt(r, p.anchor, va, tw), wc = cellWidthAt(r, p.cursor, vc, tw);
  const int lo = qMin(va, vc);
  const int hi = m_toEol ? std::numeric_limits<int>::max() : qMax(va + wa - 1, vc + wc - 1);
  SelectionList out;
  const qsizetype maxRows = qMax(1, m_ctx->settings.maxSelections);
  qsizetype from = first, to = last;
  if (to - from + 1 > maxRows) {
    from = qMax(first, lc - maxRows / 2);
    to = qMin(last, from + maxRows - 1);
  }
  for (qsizetype line = from; line <= to; ++line) {
    bool past = false;
    const qsizetype start = offsetAtVirtualColumn(r, line, lo, tw, &past);
    qsizetype end = start;
    if (!past) {
      if (hi == std::numeric_limits<int>::max()) {
        end = r.lineEnd(line);
      } else {
        bool pastEnd = false;
        const qsizetype at = offsetAtVirtualColumn(r, line, hi, tw, &pastEnd);
        end = pastEnd ? r.lineEnd(line) : bounds.nextGrapheme(at);
        end = qMin(end, r.lineEnd(line));
      }
    }
    out.append({start, end});
  }
  *primary = int(qBound<qsizetype>(0, lc - from, out.size() - 1));
  return out;
}

void VimInputHandler::writeVisual() {
  const Rope &r = rope();
  SelectionList list;
  int primary = 0;
  if (m_mode == Mode::VisualBlock) {
    if (m_vis.isEmpty())
      return;
    list = blockSelections(m_vis.constFirst(), &primary);
  } else if (m_mode == Mode::VisualLine) {
    for (const VisPoint &p : m_vis) {
      const qsizetype la = r.lineAt(p.anchor), lc = r.lineAt(p.cursor);
      const qsizetype first = qMin(la, lc), last = qMax(la, lc);
      const qsizetype start = r.lineStart(first);
      const qsizetype end = last + 1 < r.lineCount() ? r.lineStart(last + 1) : r.length();
      list.append(lc >= la ? Selection{start, end} : Selection{end, start});
    }
    primary = qBound(0, m_sel->primaryIndex(), int(list.size()) - 1);
  } else {
    for (const VisPoint &p : m_vis)
      list.append(charSelection(p));
    primary = qBound(0, m_sel->primaryIndex(), int(list.size()) - 1);
  }
  m_sel->set(list, primary);
  m_written = m_sel->selections();
  if (m_mode != Mode::VisualBlock && m_sel->count() != m_vis.size())
    rebuildVisualFromSelections();
}

void VimInputHandler::rebuildVisualFromSelections() {
  const Rope &r = rope();
  TextBoundaries bounds(r);
  m_vis.clear();
  const int n = m_sel->count();
  for (int i = 0; i < n; ++i) {
    const Selection s = m_sel->at(i);
    VisPoint p;
    if (s.isEmpty()) {
      p = {s.head, s.head};
    } else if (s.head >= s.anchor) {
      p = {s.anchor, bounds.previousGrapheme(s.head)};
    } else {
      p = {bounds.previousGrapheme(s.anchor), s.head};
    }
    m_vis.append(p);
  }
  m_written = m_sel->selections();
  m_toEol = false;
}

void VimInputHandler::enterVisual(Mode mode) {
  m_vis.clear();
  for (qsizetype head : heads())
    m_vis.append({head, head});
  if (mode == Mode::VisualBlock) {
    const int primary = qBound(0, m_sel->primaryIndex(), int(m_vis.size()) - 1);
    m_vis = {m_vis[primary]};
  }
  m_toEol = false;
  setMode(mode);
  writeVisual();
}

void VimInputHandler::exitVisual(bool restoreCursor) {
  if (!isVisualMode(m_mode))
    return;
  m_lastVisual = {m_mode, m_vis, m_toEol, true};
  qsizetype first = std::numeric_limits<qsizetype>::max(), last = 0;
  for (int i = 0; i < m_sel->count(); ++i) {
    const Selection s = m_sel->at(i);
    first = qMin(first, s.start());
    last = qMax(last, s.isEmpty() ? s.end() : qMax(s.start(), s.end() - 1));
  }
  if (first != std::numeric_limits<qsizetype>::max()) {
    setMark(u'<', first);
    setMark(u'>', last);
  }
  QList<qsizetype> cursors;
  int primary = m_sel->primaryIndex();
  if (restoreCursor && !m_vis.isEmpty() && m_written == m_sel->selections()) {
    for (const VisPoint &p : m_vis)
      cursors.append(clampNormal(p.cursor));
    if (m_mode == Mode::VisualBlock)
      primary = 0;
  } else {
    for (int i = 0; i < m_sel->count(); ++i)
      cursors.append(clampNormal(m_sel->at(i).head));
  }
  m_vis.clear();
  setMode(Mode::Normal);
  if (!cursors.isEmpty())
    setCursors(cursors, qBound(0, primary, int(cursors.size()) - 1));
}

void VimInputHandler::reselectLastVisual() {
  if (!m_lastVisual.valid)
    return;
  const Rope &r = rope();
  m_vis = m_lastVisual.points;
  for (VisPoint &p : m_vis) {
    p.anchor = qMin(p.anchor, r.length());
    p.cursor = qMin(p.cursor, r.length());
  }
  m_toEol = m_lastVisual.toEol;
  setMode(m_lastVisual.mode);
  writeVisual();
}

void VimInputHandler::visualSwapEnds(bool horizontal) {
  const Rope &r = rope();
  if (horizontal && m_mode == Mode::VisualBlock && !m_vis.isEmpty()) {
    VisPoint &p = m_vis.first();
    const int tw = tabWidth();
    const int va = virtualColumn(r, p.anchor, tw), vc = virtualColumn(r, p.cursor, tw);
    const qsizetype la = r.lineAt(p.anchor), lc = r.lineAt(p.cursor);
    const qsizetype newAnchor = offsetAtVirtualColumn(r, la, vc, tw);
    const qsizetype newCursor = offsetAtVirtualColumn(r, lc, va, tw);
    p.anchor = newAnchor;
    p.cursor = newCursor;
  } else {
    for (VisPoint &p : m_vis)
      std::swap(p.anchor, p.cursor);
  }
  writeVisual();
}

void VimInputHandler::visualObject(const QStringList &keys, int count) {
  const Rope &r = rope();
  TextBoundaries bounds(r);
  bool linewise = false;
  bool any = false;
  for (VisPoint &p : m_vis) {
    qsizetype selStart = p.cursor, selEnd = p.cursor;
    if (p.anchor != p.cursor) {
      const Selection s = charSelection(p);
      selStart = s.start();
      selEnd = s.end();
    }
    const ObjectRange o =
      textObject(r, p.cursor, keys[1][0], keys[0] == u"a"_s, qMax(1, count), selStart, selEnd);
    if (!o.ok || o.end < o.start)
      continue;
    any = true;
    linewise |= o.linewise;
    p.anchor = o.start;
    p.cursor = o.end > o.start ? bounds.previousGrapheme(o.end) : o.start;
  }
  if (!any) {
    m_failed = true;
    return;
  }
  if (linewise && m_mode == Mode::Visual)
    setMode(Mode::VisualLine);
  writeVisual();
}

QList<VimInputHandler::Range> VimInputHandler::visualRanges(bool *linewise, bool *block) const {
  const Rope &r = rope();
  QList<Range> out;
  *linewise = m_mode == Mode::VisualLine;
  *block = m_mode == Mode::VisualBlock;
  const int n = m_sel->count();
  for (int i = 0; i < n; ++i) {
    const Selection s = m_sel->at(i);
    Range rg;
    rg.start = s.start();
    rg.end = s.end();
    rg.linewise = *linewise;
    rg.cursor = rg.start;
    if (*linewise && i < m_vis.size()) {
      const qsizetype line = r.lineAt(rg.start);
      rg.cursor = offsetAtVirtualColumn(r, line, virtualColumn(r, m_vis[i].cursor, tabWidth()), tabWidth());
    }
    out.append(rg);
  }
  return out;
}

void VimInputHandler::blockInsert(bool append) {
  const Rope &r = rope();
  const int tw = tabWidth();
  if (m_vis.isEmpty())
    return;
  const VisPoint p = m_vis.first();
  const int va = virtualColumn(r, p.anchor, tw), vc = virtualColumn(r, p.cursor, tw);
  const int wa = cellWidthAt(r, p.anchor, va, tw), wc = cellWidthAt(r, p.cursor, vc, tw);
  const int lo = qMin(va, vc);
  const int hi = qMax(va + wa - 1, vc + wc - 1);
  const qsizetype la = r.lineAt(p.anchor), lc = r.lineAt(p.cursor);
  const qsizetype first = qMin(la, lc), last = qMax(la, lc);
  const bool toEol = m_toEol;
  exitVisual(false);
  setMode(Mode::Normal);
  QList<Edit> pad;
  QList<qsizetype> rows;
  for (qsizetype line = first; line <= last; ++line) {
    const qsizetype end = r.lineEnd(line);
    if (append) {
      if (toEol) {
        rows.append(line);
        continue;
      }
      const int width = virtualColumn(r, end, tw);
      if (width < hi + 1)
        pad.append({end, end, QString(hi + 1 - width, u' ')});
      rows.append(line);
    } else {
      bool past = false;
      offsetAtVirtualColumn(r, line, lo, tw, &past);
      if (!past || r.lineLength(line) == 0 || lo == 0)
        rows.append(line);
    }
  }
  if (rows.isEmpty())
    return;
  EditResult res;
  if (!pad.isEmpty())
    res = edit(pad);
  const Rope &after = rope();
  QList<qsizetype> cursors;
  for (qsizetype line : rows) {
    if (append) {
      cursors.append(toEol ? after.lineEnd(line) : offsetAtVirtualColumn(after, line, hi + 1, tw));
      if (!toEol && cursors.last() > after.lineEnd(line))
        cursors.last() = after.lineEnd(line);
    } else {
      cursors.append(offsetAtVirtualColumn(after, line, lo, tw));
    }
  }
  m_blockOrigin = offsetAtVirtualColumn(after, first, lo, tw);
  setCursors(cursors, 0);
  startInsert(1);
  m_blockInsert = true;
}

void VimInputHandler::executeVisualCommand(const Cmd &cmd) {
  const QString &k = cmd.keys.constFirst();
  const int count = cmd.effective();
  const int n = qMax(1, count);
  const Rope &r = rope();
  const QChar regName = cmd.reg.isEmpty() ? QChar() : cmd.reg[0];
  bool linewise = false, block = false;

  auto captureDot = [&] {
    QList<Range> ranges = visualRanges(&linewise, &block);
    if (ranges.isEmpty())
      return;
    Dot d;
    d.valid = true;
    d.visualMode = m_mode;
    const Range &first = ranges.first();
    const Range &lastRange = block ? ranges.last() : ranges.first();
    const qsizetype la = r.lineAt(first.start);
    const qsizetype lb = r.lineAt(qMax(
      first.start, (block ? lastRange.end : first.end) - (first.linewise || first.end <= first.start ? 0 : 1)
    ));
    d.lines = block ? ranges.size() : lb - la + 1;
    if (m_mode == Mode::VisualBlock && !m_vis.isEmpty()) {
      const VisPoint p = m_vis.first();
      const int tw = tabWidth();
      const int va = virtualColumn(r, p.anchor, tw), vc = virtualColumn(r, p.cursor, tw);
      d.columns = qAbs(va - vc) + 1;
    } else if (d.lines <= 1) {
      d.columns = first.end - first.start;
    } else {
      d.columns = first.end - r.lineStart(lb);
    }
    m_visDot = d;
  };
  // Leaves visual mode before an operator edits (the operator sets the cursors itself).
  auto leave = [&] {
    m_lastVisual = {m_mode, m_vis, m_toEol, true};
    qsizetype first = std::numeric_limits<qsizetype>::max(), last = 0;
    for (int i = 0; i < m_sel->count(); ++i) {
      const Selection s = m_sel->at(i);
      first = qMin(first, s.start());
      last = qMax(last, s.isEmpty() ? s.end() : qMax(s.start(), s.end() - 1));
    }
    if (first != std::numeric_limits<qsizetype>::max()) {
      setMark(u'<', first);
      setMark(u'>', last);
    }
    m_vis.clear();
    setMode(Mode::Normal);
  };
  auto wholeLines = [&](QList<Range> ranges) {
    for (Range &rg : ranges) {
      const qsizetype a = r.lineAt(rg.start);
      const qsizetype b = r.lineAt(rg.end > rg.start ? rg.end - 1 : rg.start);
      rg.start = r.lineStart(a);
      rg.end = b + 1 < r.lineCount() ? r.lineStart(b + 1) : r.length();
      rg.linewise = true;
    }
    return ranges;
  };
  auto toLineEnds = [&](QList<Range> ranges) {
    for (Range &rg : ranges)
      rg.end = r.lineEnd(r.lineAt(rg.start));
    return ranges;
  };

  if (k == u"v"_s || k == u"V"_s || k == u"<C-v>"_s) {
    const Mode target = k == u"v"_s ? Mode::Visual : k == u"V"_s ? Mode::VisualLine : Mode::VisualBlock;
    if (m_mode == target) {
      exitVisual();
    } else {
      if (target == Mode::VisualBlock && m_vis.size() > 1) {
        const int primary = qBound(0, m_sel->primaryIndex(), int(m_vis.size()) - 1);
        m_vis = {m_vis[primary]};
      }
      setMode(target);
      writeVisual();
    }
    return;
  }
  if (k == u"o"_s) {
    visualSwapEnds(false);
    return;
  }
  if (k == u"O"_s) {
    visualSwapEnds(true);
    return;
  }
  if (k == u"g"_s && cmd.keys[1] == u"v"_s) {
    const LastVisual previous = m_lastVisual;
    m_lastVisual = {m_mode, m_vis, m_toEol, true};
    if (previous.valid) {
      m_vis = previous.points;
      m_toEol = previous.toEol;
      setMode(previous.mode);
      writeVisual();
    }
    return;
  }

  const bool isOp =
    k == u"d"_s || k == u"x"_s || k == u"<Del>"_s || k == u"X"_s || k == u"D"_s || k == u"y"_s ||
    k == u"Y"_s || k == u"c"_s || k == u"s"_s || k == u"C"_s || k == u"S"_s || k == u"R"_s || k == u">"_s ||
    k == u"<"_s || k == u"~"_s || k == u"u"_s || k == u"U"_s ||
    (k == u"g"_s && (cmd.keys[1] == u"~"_s || cmd.keys[1] == u"u"_s || cmd.keys[1] == u"U"_s));
  if (isOp) {
    const bool yank = k == u"y"_s || k == u"Y"_s;
    if (!yank)
      m_cmdChange = true;
    if (!yank)
      captureDot();
    QList<Range> ranges = visualRanges(&linewise, &block);
    leave();
    if (
      k == u"X"_s || k == u"Y"_s || k == u"S"_s || k == u"R"_s || ((k == u"D"_s || k == u"C"_s) && !block)
    ) {
      ranges = wholeLines(ranges);
      block = false;
    } else if ((k == u"D"_s || k == u"C"_s) && block) {
      ranges = toLineEnds(ranges);
    }
    m_cmdReg = cmd.reg;
    if (k == u"d"_s || k == u"x"_s || k == u"<Del>"_s || k == u"X"_s || k == u"D"_s) {
      applyOperator(u"d"_s, ranges, cmd, block);
    } else if (yank) {
      applyOperator(u"y"_s, ranges, cmd, block);
    } else if (k == u"c"_s || k == u"s"_s || k == u"C"_s || k == u"S"_s || k == u"R"_s) {
      applyOperator(u"c"_s, ranges, cmd, block);
    } else if (k == u">"_s || k == u"<"_s) {
      shiftLines(ranges, k == u">"_s, n);
    } else if (k == u"~"_s) {
      changeCase(ranges, u"g~"_s);
    } else if (k == u"u"_s) {
      changeCase(ranges, u"gu"_s);
    } else if (k == u"U"_s) {
      changeCase(ranges, u"gU"_s);
    } else {
      changeCase(ranges, u"g"_s + cmd.keys[1]);
    }
    if (block && m_mode == Mode::Normal && m_sel->count() > 1) {
      m_sel->setPrimary(0);
      m_sel->collapseToPrimary();
    }
    return;
  }
  if (k == u"J"_s || (k == u"g"_s && cmd.keys[1] == u"J"_s)) {
    m_cmdChange = true;
    captureDot();
    QList<Range> ranges = visualRanges(&linewise, &block);
    leave();
    QList<qsizetype> starts;
    qsizetype lines = 2;
    if (!ranges.isEmpty()) {
      const qsizetype a = r.lineAt(ranges.first().start);
      const qsizetype b =
        r.lineAt(ranges.last().end > ranges.last().start ? ranges.last().end - 1 : ranges.last().start);
      lines = qMax<qsizetype>(2, b - a + 1);
      starts.append(r.lineStart(a));
    }
    setCursors(starts);
    joinLines(int(lines), k == u"J"_s);
    return;
  }
  if (k == u"p"_s || k == u"P"_s) {
    m_cmdChange = true;
    captureDot();
    replaceSelectionWithRegister(regName, k == u"p"_s);
    return;
  }
  if (k == u"r"_s) {
    m_cmdChange = true;
    captureDot();
    QList<Range> ranges = visualRanges(&linewise, &block);
    leave();
    // Every selected character (not line breaks) becomes the typed one.
    const QString ch = cmd.keys[1] == u"<Tab>"_s ? u"\t"_s : cmd.keys[1];
    if (!isPrintableSymbol(ch) && ch != u"\t"_s) {
      setCursors({ranges.isEmpty() ? 0 : ranges.first().start});
      return;
    }
    QList<Edit> edits;
    QList<qsizetype> cursors;
    for (const Range &rg : ranges) {
      const QString text = r.toString(rg.start, rg.end);
      QString replaced;
      for (qsizetype i = 0; i < text.size();) {
        const QChar c = text[i];
        if (c == u'\n' || c == u'\r') {
          replaced += c;
          ++i;
          continue;
        }
        replaced += ch;
        i += c.isHighSurrogate() && i + 1 < text.size() ? 2 : 1;
      }
      if (replaced != text)
        edits.append({rg.start, rg.end, replaced});
      cursors.append(rg.start);
    }
    if (!edits.isEmpty())
      edit(edits);
    setCursors(block ? cursors.mid(0, 1) : cursors);
    return;
  }
  if (k == u"I"_s || k == u"A"_s) {
    m_cmdChange = true;
    if (m_mode == Mode::VisualBlock) {
      blockInsert(k == u"A"_s);
      return;
    }
    QList<qsizetype> cursors;
    for (int i = 0; i < m_sel->count(); ++i) {
      const Selection s = m_sel->at(i);
      cursors.append(k == u"I"_s ? s.start() : s.end());
    }
    leave();
    setCursors(cursors);
    startInsert(1);
    return;
  }
  executeCommand(cmd);
}

} // namespace qce
