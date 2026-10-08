#ifndef QCE_FINDREPLACE_H
#define QCE_FINDREPLACE_H

#include "core/commands.h"
#include "core/selectionset.h"
#include "core/textdocument.h"
#include "core/textsearch.h"

#include <QtCore/QFutureWatcher>
#include <QtCore/QObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QTimer>

#include <atomic>
#include <functional>
#include <memory>

namespace qce {

// Find and replace over one document (API-04; ADR 0019). The query is plain text or a regular
// expression; while `active` every match is counted on a worker thread that reads an immutable
// snapshot of the rope, the result being dropped when the query or the text has moved on. The match
// list drives next/previous by binary search, the editor highlights matches in view from
// highlightPattern(), and replaceAll() builds its edits on a snapshot too and applies them as a
// single undo step. Everything is line by line, as vim's search is, so a pattern never spans lines.
class FindReplace : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged FINAL)
  Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged FINAL)
  Q_PROPERTY(QString replacement READ replacement WRITE setReplacement NOTIFY replacementChanged FINAL)
  Q_PROPERTY(bool regex READ regex WRITE setRegex NOTIFY optionsChanged FINAL)
  Q_PROPERTY(bool caseSensitive READ caseSensitive WRITE setCaseSensitive NOTIFY optionsChanged FINAL)
  Q_PROPERTY(bool wholeWord READ wholeWord WRITE setWholeWord NOTIFY optionsChanged FINAL)
  Q_PROPERTY(int matchCount READ matchCount NOTIFY resultsChanged FINAL)
  Q_PROPERTY(bool capped READ capped NOTIFY resultsChanged FINAL)
  Q_PROPERTY(int currentIndex READ currentIndex NOTIFY resultsChanged FINAL)
  Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
  Q_PROPERTY(QString error READ error NOTIFY errorChanged FINAL)
public:
  // Matches counted and listed at most; past it `capped` is set and next/previous search the text.
  static constexpr int kMaxMatches = 100'000;

  using ContextProvider = std::function<EditContext()>;
  FindReplace(TextDocument &document, SelectionSet &selections, ContextProvider context, QObject *parent = nullptr);
  ~FindReplace() override;

  bool active() const { return m_active; }
  // Searching starts when set, and the first match from the cursor is selected as the text changes.
  void setActive(bool active);
  QString text() const { return m_text; }
  void setText(const QString &text);
  QString replacement() const { return m_replacement; }
  void setReplacement(const QString &replacement);
  // Regular expression (QRegularExpression syntax) rather than plain text. A regex replacement
  // expands $1 $& $$ \n \t; a plain-text one is inserted as it is.
  bool regex() const { return m_query.regex; }
  void setRegex(bool on);
  bool caseSensitive() const { return m_query.caseSensitive; }
  void setCaseSensitive(bool on);
  bool wholeWord() const { return m_query.wholeWord; }
  void setWholeWord(bool on);

  // Matches in the document as of the last finished search (0 while searching the first time).
  int matchCount() const { return int(m_matches.size()); }
  bool capped() const { return m_capped; }
  // Which match is selected (0-based), or -1.
  int currentIndex() const { return m_current; }
  // A search or a replace-all is running.
  bool busy() const { return m_searching || m_replacing; }
  // Why the expression is invalid; empty otherwise.
  QString error() const { return m_pattern.error; }

  // What the editor marks: the expression while active and valid, else an empty one.
  QRegularExpression highlightPattern() const;

  // Selects the next or previous match from the primary selection, wrapping. False when none.
  Q_INVOKABLE bool next() { return step(true); }
  Q_INVOKABLE bool previous() { return step(false); }
  // Replaces the selected match (when the selection is one) and moves to the next. Without a match
  // selected it only moves to the next.
  Q_INVOKABLE bool replace();
  // Replaces every match; one undo step. Finishes later: busy is set meanwhile.
  Q_INVOKABLE void replaceAll();
  // One selection per match (at most the editor's selection limit).
  Q_INVOKABLE bool selectAllMatches();
  // Seeds the text with the single-line selection, or else the word at the cursor.
  Q_INVOKABLE void useSelection();

signals:
  void activeChanged();
  void textChanged();
  void replacementChanged();
  void optionsChanged();
  void resultsChanged();
  void busyChanged();
  void errorChanged();
  // The pattern the editor should mark changed.
  void highlightChanged();
  // The selection moved to a match: scroll it into view.
  void revealRequested();
  // The selection was changed (by next, replace, ...).
  void selectionMoved();

private:
  struct SearchResult {
    quint64 generation = 0;
    quint64 version = 0;
    QList<Selection> matches;
    bool capped = false;
  };
  struct ReplaceResult {
    quint64 generation = 0;
    quint64 version = 0;
    QList<commands::Replacement> edits;
    bool cancelled = false;
  };

  void queryChanged();
  void startSearch();
  void searchFinished();
  void replaceFinished();
  void documentChanged();
  void selectionChanged();
  void cancelJobs();
  void setBusyFlags(bool searching, bool replacing);
  void clearResults();
  bool fresh() const { return m_matchesVersion == m_document.version() && m_hasMatches; }
  void updateCurrent();
  void selectMatch(const Selection &match);
  bool step(bool forward);
  bool jumpFromOrigin();
  std::optional<QString> replacementAt(const Selection &match) const;
  void applyEdits(const QList<commands::Replacement> &edits);

  TextDocument &m_document;
  SelectionSet &m_selections;
  ContextProvider m_context;

  bool m_active = false;
  QString m_text, m_replacement;
  search::Query m_query;
  search::Pattern m_pattern;

  QList<Selection> m_matches;
  quint64 m_matchesVersion = 0;
  bool m_hasMatches = false;
  bool m_capped = false;
  int m_current = -1;

  quint64 m_generation = 0;
  quint64 m_replaceGeneration = 0;
  std::shared_ptr<std::atomic_bool> m_cancel, m_replaceCancel;
  QFutureWatcher<SearchResult> m_searchWatcher;
  QFutureWatcher<ReplaceResult> m_replaceWatcher;
  bool m_searching = false, m_replacing = false;
  QTimer m_debounce;

  qsizetype m_origin = 0;      // where an incremental search starts: the cursor before the query was typed
  bool m_pendingJump = false;  // select the first match from m_origin when a result arrives
  bool m_ownSelection = false; // the selection is being changed by this object
};

} // namespace qce

#endif // QCE_FINDREPLACE_H
