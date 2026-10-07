#ifndef QCE_VIM_VIMHANDLER_H
#define QCE_VIM_VIMHANDLER_H

#include "core/anchorset.h"
#include "core/inputhandler.h"
#include "core/vim/vimregex.h"

#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QStringList>

namespace qce {

// Vim as an InputHandler (ADR 0005, ADR 0017): normal, insert, replace and the three visual modes,
// operators over motions and text objects, registers, dot-repeat, macros, marks and the jump list,
// search and a subset of ex commands. It works on every selection of the set independently, so
// vim's editing is multi-cursor editing, and it edits only through commands, so everything it does
// is undoable in the editor's own history (one undo step per vim change, an insert session is one).
//
// The cursor model: the SelectionSet stays between characters. In normal mode every selection is an
// empty cursor on a character (never past the last one of a line); in visual modes the selection is
// the inclusive range; the handler keeps the anchor and cursor characters itself.
class VimInputHandler : public QObject, public InputHandler {
  Q_OBJECT
  Q_PROPERTY(Mode mode READ mode NOTIFY modeChanged FINAL)
  Q_PROPERTY(QString modeName READ modeName NOTIFY modeChanged FINAL)
  Q_PROPERTY(QString commandLine READ commandLine NOTIFY pendingChanged FINAL)
  Q_PROPERTY(QString pendingKeys READ pendingKeys NOTIFY pendingChanged FINAL)
  Q_PROPERTY(QString recordingRegister READ recordingRegister NOTIFY recordingChanged FINAL)
  Q_PROPERTY(QString message READ message NOTIFY messageChanged FINAL)
  Q_PROPERTY(bool ignoreCase READ ignoreCase WRITE setIgnoreCase NOTIFY optionsChanged FINAL)
  Q_PROPERTY(bool smartCase READ smartCase WRITE setSmartCase NOTIFY optionsChanged FINAL)

public:
  enum class Mode : quint8 {
    Normal,
    Insert,
    Replace,
    Visual,
    VisualLine,
    VisualBlock,
    OperatorPending,
    CommandLine
  };
  Q_ENUM(Mode)

  // A register's content: `text` is what pastes; `pieces` has one entry per cursor that yanked it.
  struct Register {
    enum class Type : quint8 { Char, Line, Block };
    QString text;
    QStringList pieces;
    Type type = Type::Char;
    bool isEmpty() const { return text.isEmpty() && pieces.isEmpty(); }
  };

  explicit VimInputHandler(QObject *parent = nullptr);
  ~VimInputHandler() override;

  // InputHandler
  bool keyPress(QKeyEvent *event, EditContext &ctx, InputHost &host) override;
  void reset() override;
  void activate(EditContext &ctx, InputHost &host) override;
  void deactivate(EditContext &ctx, InputHost &host) override;
  CursorShape cursorShape() const override;
  // Block cursor: the character the cursor is on (in visual modes the end that moves); -1 for the
  // selections of a block that carry no cursor of their own.
  qsizetype cursorOffset(int index, const Selection &selection, const EditContext &ctx) const override;
  bool acceptsTextInput() const override;
  bool commitText(const QString &text, EditContext &ctx, InputHost &host) override;

  // Runs keys given in vim notation ("dd", "3x", "ihello<Esc>"); false when a key was not handled.
  bool feed(const QString &notation, EditContext &ctx, InputHost &host);

  Mode mode() const;
  QString modeName() const;
  // What has been typed of an operator or command (vim's showcmd), and of a ":" or search prompt.
  QString pendingKeys() const;
  QString commandLine() const;
  QString recordingRegister() const { return m_recordReg; }
  // The last message or error ("E486: Pattern not found: x"), cleared by the next key.
  QString message() const { return m_message; }

  bool ignoreCase() const { return m_options.ignoreCase; }
  void setIgnoreCase(bool on);
  bool smartCase() const { return m_options.smartCase; }
  void setSmartCase(bool on);

  // Register access for hosts and tests. Names are vim's: a-z, A-Z (append), 0-9, ", -, _, ., :, /.
  Register readRegister(QChar name) const;
  void setRegister(QChar name, const QString &text, Register::Type type = Register::Type::Char);

signals:
  void modeChanged();
  void pendingChanged();
  void recordingChanged();
  void messageChanged();
  void optionsChanged();
  // :w [path], :q, :wq, :x and ZZ/ZQ for the host to act on; `force` is the "!".
  void writeRequested(const QString &path);
  void quitRequested(bool force);
  // An ex command vim does not know, as typed (without the colon).
  void exCommand(const QString &command);

private:
  struct VisPoint {
    qsizetype anchor = 0;
    qsizetype cursor = 0;
  };
  enum class Kind : quint8 { Exclusive, Inclusive, Linewise };
  enum class Parse : quint8 { Incomplete, Complete, Invalid };

  // A parsed normal-mode command: [count]["reg][count]{operator}[count]{motion}, or a motion, or a
  // plain command. `keys` is the motion or command without counts (for a doubled operator "<line>").
  struct Cmd {
    enum class Type : quint8 { Motion, Operator, Command };
    Type type = Type::Command;
    int count = 0;  // before the operator or command; 0 = none typed
    int count2 = 0; // after the operator
    QString reg;
    QString op; // d c y > < g~ gu gU
    QStringList keys;
    bool doubled = false;
    int prefixLen = 0; // symbols taken by the count and register
    int cmdline = -1;  // index of the ":" / "/" / "?" a prompt started at, when it is still open
    bool hasCount() const { return count > 0 || count2 > 0; }
    int effective() const { return hasCount() ? qMax(1, count) * qMax(1, count2) : 0; }
  };
  // Where a motion lands (or, for a text object, the range it selects).
  struct Target {
    bool ok = false;
    qsizetype from = 0;
    qsizetype to = 0;
    Kind kind = Kind::Exclusive;
    bool object = false;
    bool linewise = false; // an object that covers whole lines
    bool jump = false;
    bool bigMotion = false; // a deletion with it fills register 1 even when small
    int goal = -2;          // display column for the next j/k; -1 end of line, -2 unchanged
    qreal goalX = SelectionSet::NoGoal;
  };
  // What an operator works on: [start, end), whole lines when `linewise`.
  struct Range {
    qsizetype start = 0;
    qsizetype end = 0;
    bool linewise = false;
    qsizetype cursor = 0; // where the cursor goes after a yank
  };
  struct EditResult {
    bool ok = false;
    QList<qsizetype> starts; // where each replacement begins in the new text
    QList<qsizetype> ends;   // and where it ends
  };

  // The dot-repeat record: the keys of the last change without its count and register.
  struct Dot {
    QStringList keys;
    int count = 0;
    QString reg;
    bool valid = false;
    bool visual = false; // repeated over a region of the same size as the original selection
    Mode visualMode = Mode::Visual;
    qsizetype lines = 0; // lines spanned, and for characterwise the columns of the last line
    qsizetype columns = 0;
  };

  struct SearchState {
    QString pattern;
    bool forward = true;
    vim::CompiledPattern compiled;
  };

  // --- entry
  bool process(const QString &symbol);
  bool processInsert(const QString &symbol);
  bool processReplace(const QString &symbol);
  bool processNormal(const QString &symbol);
  bool processInsertKey(const QString &symbol);
  void finishCommand();
  void replayDot(int count);
  void replayKeys(const QStringList &keys);
  void runMacro(QChar name, int count);
  bool exitCmdlineKey(const QString &symbol, const Cmd &probe);

  // --- parsing (vimhandler.cpp)
  Parse parse(const QStringList &keys, Cmd &cmd) const;
  int motionLength(const QStringList &keys, int at, bool objects) const;
  int commandLength(const QStringList &keys, int at) const;
  int operatorAt(const QStringList &keys, int at, QString *op) const;

  // --- execution (vimhandler.cpp)
  void execute(const Cmd &cmd);
  void executeMotion(const Cmd &cmd);
  void executeOperator(const Cmd &cmd);
  void executeVisualCommand(const Cmd &cmd);
  void executeCommand(const Cmd &cmd);
  Target
  evalMotion(const QStringList &keys, int count, qsizetype offset, int index, const QString &op, bool visual);
  Target
  evalSearch(const QStringList &keys, int count, qsizetype offset, bool flip, bool backwardKey, bool useLast);
  Range rangeFor(const Target &target, qsizetype cursor) const;

  // --- operators and edits (vimoperators.cpp)
  bool applyOperator(const QString &op, QList<Range> ranges, const Cmd &cmd, bool block = false);
  void deleteRanges(const QList<Range> &ranges, bool enterInsert, bool block);
  void changeCase(const QList<Range> &ranges, const QString &op);
  void shiftLines(const QList<Range> &ranges, bool right, int amount);
  void joinLines(int count, bool spaces);
  void pasteRegister(QChar name, bool after, int count, bool cursorAfter = false);
  void replaceSelectionWithRegister(QChar name, bool swapRegister);
  void replaceChars(const QString &ch, int count);
  void toggleCaseAtCursor(int count);
  void incrementNumber(int delta);
  void openLine(bool below, int count);
  void yankRanges(const QList<Range> &ranges, const QString &reg, bool isDelete, bool bigMotion, bool block);
  struct Edit {
    qsizetype start = 0;
    qsizetype end = 0;
    QString text;
  };
  EditResult edit(const QList<Edit> &edits);

  // --- insert mode
  void startInsert(int count = 1, bool openedLines = false);
  void finishInsert();
  void startReplace(int count = 1);
  void repeatInsert(int times);

  // --- visual mode
  void enterVisual(Mode mode);
  void exitVisual(bool restoreCursor = true);
  void writeVisual();
  void rebuildVisualFromSelections();
  void syncExternal();
  QList<Range> visualRanges(bool *linewise, bool *block) const;
  Selection charSelection(const VisPoint &p) const;
  SelectionList blockSelections(const VisPoint &p, int *primary) const;
  void blockInsert(bool append);
  void visualSwapEnds(bool horizontal);
  void visualObject(const QStringList &keys, int count);
  void reselectLastVisual();

  // --- cursors, groups, helpers
  QList<qsizetype> heads() const;
  QList<qsizetype> cursorPositions() const;
  void setCursors(const QList<qsizetype> &cursors, int primary = -1);
  void clampCursors();
  qsizetype clampNormal(qsizetype offset) const;
  void ensureGroup();
  void closeGroup();
  void setMode(Mode mode);
  void setMessage(const QString &message);
  void setPending(const QStringList &keys);
  void clearPending();
  const Rope &rope() const;
  int tabWidth() const;
  int goalColumn(int index, qsizetype offset);
  void commitGoals(const QList<int> &goals);
  void afterUndo();
  QString indentUnit() const;
  QString indentOfLine(qsizetype line, qsizetype upTo = -1) const;

  // --- registers, marks, jumps (vimoperators.cpp)
  void storeRegister(
    const QString &reg, const QStringList &pieces, Register::Type type, bool isDelete, bool bigMotion
  );
  Register fetchRegister(QChar name) const;
  void setMark(QChar name, qsizetype offset);
  qsizetype markOffset(QChar name) const;
  void pushJump(qsizetype offset);
  void jumpOlder(int count, bool older);

  // --- search and ex (vimex.cpp)
  bool compileSearch(const QString &pattern, vim::CompiledPattern *out);
  void highlightSearch();
  struct ExRange {
    qsizetype first = 0;
    qsizetype last = 0;
    bool given = false;
  };
  void executeEx(const QString &line, bool nested = false);
  bool parseExRange(const QString &text, qsizetype *pos, ExRange *range);
  void exSubstitute(const ExRange &range, const QString &args, bool global);
  void exGlobal(const ExRange &range, const QString &args, bool invert);
  void exDelete(const ExRange &range, const QString &args, bool yank);
  void exNormal(const ExRange &range, const QString &args);
  void exSet(const QString &args);

  Mode m_mode = Mode::Normal;
  QStringList m_pending;
  bool m_opPending = false;
  QStringList m_history[2]; // ex, search
  int m_historyIndex = -1;

  // Visual
  QList<VisPoint> m_vis;
  bool m_toEol = false;
  SelectionList m_written;
  struct LastVisual {
    Mode mode = Mode::Visual;
    QList<VisPoint> points;
    bool toEol = false;
    bool valid = false;
  } m_lastVisual;

  // Insert
  QStringList m_insertKeys;
  int m_insertCount = 1;
  bool m_insertOpened = false; // the session began with o/O: repeats open lines
  bool m_blockInsert = false;
  qsizetype m_blockOrigin = 0;     // where the cursor goes when a block insert ends: the block's top-left
  bool m_insertFromNormal = false; // <C-o>: back to insert after one command
  QStringList m_insertPending;     // <C-r>, <C-v> waiting for their argument
  struct ReplaceStep {
    QStringList removed;
    QStringList typed;
  };
  QList<ReplaceStep> m_replaceStack;
  QString m_lastInserted;

  // Undo group
  bool m_group = false;

  // Registers, find, search
  QHash<QChar, Register> m_regs;
  QString m_lastFind;
  bool m_lastFindForward = true;
  bool m_lastFindTill = false;
  SearchState m_search;
  vim::PatternOptions m_options;
  QString m_lastEx;
  QString m_lastSubstitutePattern;

  // Dot, macros
  QStringList m_cmdKeys;
  int m_cmdPrefix = 0;
  bool m_cmdChange = false;
  int m_cmdCount = 0;
  QString m_cmdReg;
  bool m_failed = false;
  bool m_inDotReplay = false;
  int m_groupHold = 0; // while > 0 the undo group stays open (macros, :g, :normal are one step)
  bool m_noHighlight = false;
  bool m_hlsearch = true;
  bool m_bigMotion = false; // the operator's motion sends even small deletes to register 1
  bool m_inRepeat = false;
  Dot m_visDot;
  Dot m_dot;
  int m_depth = 0; // nested replays (dot, macros, :normal)
  QString m_recordReg;
  QStringList m_recordKeys;
  QString m_lastMacro;

  // Marks and jumps
  QHash<QChar, AnchorId> m_marks;
  QList<AnchorId> m_jumps;
  int m_jumpIndex = 0;

  // Goal columns for j/k
  QList<int> m_goal;
  QList<qsizetype> m_goalFor;

  QString m_message;

  // Valid only inside process().
  EditContext *m_ctx = nullptr;
  InputHost *m_host = nullptr;
  TextDocument *m_doc = nullptr;
  SelectionSet *m_sel = nullptr;
};

} // namespace qce

#endif // QCE_VIM_VIMHANDLER_H
