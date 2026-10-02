#ifndef QCE_TREESITTERHIGHLIGHTER_H
#define QCE_TREESITTERHIGHLIGHTER_H

#include "core/highlighter.h"
#include "core/textchange.h"
#include "core/textdocument.h"
#include "syntax/languageregistry.h"
#include "syntax/parsejob.h"

#include <QtCore/QCache>
#include <QtCore/QList>
#include <QtCore/QString>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <memory>
#include <vector>

namespace qce {

struct HighlightMailbox;

// Highlights from tree-sitter (M6). The document is parsed on a worker thread over rope snapshots;
// the GUI thread keeps the last tree, applies every edit to it at once (so spans stay in place
// while the next parse runs, SYNTAX-09) and answers highlightLines() from a cache of 64-line
// blocks filled by running the highlight query over just those lines (SYNTAX-05).
//
// Big documents: the first parse covers a window around the viewport; a whole-document parse
// follows only below `fullParseLimit`. Above it, scrolling out of the window parses a new one
// (SYNTAX-11). Embedded languages (Markdown fences, inline Markdown) are parsed by the worker too
// and painted over the host (SYNTAX-08).
class TreeSitterHighlighter : public Highlighter {
  Q_OBJECT
  QML_NAMED_ELEMENT(SyntaxHighlighter)
  // Language id or alias; empty detects from `fileName` and the first line (shebang). An unknown
  // id (or "plain") means no highlighting.
  Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
  Q_PROPERTY(QString fileName READ fileName WRITE setFileName NOTIFY fileNameChanged)
  // Id of the language in use ("" for plain text).
  Q_PROPERTY(QString detectedLanguage READ detectedLanguage NOTIFY detectedLanguageChanged)
  // Documents longer than this many UTF-16 units are only ever parsed one window at a time.
  Q_PROPERTY(qsizetype fullParseLimit READ fullParseLimit WRITE setFullParseLimit NOTIFY fullParseLimitChanged)
  Q_PROPERTY(bool parsing READ parsing NOTIFY parsingChanged)
public:
  struct Stats {
    quint64 started = 0;
    quint64 landed = 0;
    quint64 discarded = 0; // stale generation or cancelled
    quint64 windowParses = 0;
    quint64 fullParses = 0;
    qint64 lastParseNs = 0;
    qint64 lastInjectionNs = 0;
    quint64 blocksComputed = 0;
    qint64 lastBlockNs = 0;
    bool haveFullTree = false;
    bool haveTree = false;
  };

  explicit TreeSitterHighlighter(QObject *parent = nullptr);
  ~TreeSitterHighlighter() override;

  QString language() const { return m_languageOverride; }
  void setLanguage(const QString &language);
  QString fileName() const { return m_fileName; }
  void setFileName(const QString &fileName);
  QString detectedLanguage() const { return m_lang ? m_lang->info->id : QString(); }
  // Ids of the languages that can be chosen with `language` (also callable from QML).
  Q_INVOKABLE static QStringList availableLanguages();
  Q_INVOKABLE static QString languageName(const QString &id);

  qsizetype fullParseLimit() const { return m_fullLimit; }
  void setFullParseLimit(qsizetype units);
  // Documents up to this size are parsed whole on the first parse; larger ones start with a window
  // of about this many units around the viewport. Mainly for tests and benchmarks.
  qsizetype windowSize() const { return m_windowCap; }
  void setWindowSize(qsizetype units);

  bool parsing() const { return m_running; }
  Stats stats() const { return m_stats; }
  // Extent of the current tree in UTF-16 units; the whole document when it is a full parse.
  std::pair<qsizetype, qsizetype> parsedRange() const;
  bool hasFullTree() const { return m_tree && !m_treeWindowed; }
  // The tree's S-expression, for tests.
  QString debugTree() const;

  void attach(TextDocument *document) override;
  void detach() override;

  QList<QList<HighlightSpan>>
  highlightLines(const TextSnapshot &text, qsizetype firstLine, qsizetype lastLine) override;

signals:
  void languageChanged();
  void fileNameChanged();
  void detectedLanguageChanged();
  void fullParseLimitChanged();
  void parsingChanged();
  // A parse result was installed (for the document version it was made from).
  void parseFinished(quint64 version);

private:
  friend struct HighlightMailbox;

  struct LoggedEdit {
    quint64 version;
    TSInputEdit edit;
    qsizetype startLine, oldEndLine, newEndLine;
  };
  struct Block {
    QList<QList<HighlightSpan>> lines;
  };
  static constexpr qsizetype BlockLines = 64;

  void onDocumentChanged(const TextChange &change);
  void onDocumentReset();
  void onLoadFinished();
  void redetectLanguage();
  void resetState();
  void dropTrees();

  bool planWindowed() const;
  void scheduleParse();
  void startJob();
  void applyResult(ParseResult result);

  bool covered(qsizetype unit) const;
  void dropBlocks(qsizetype firstLine, qsizetype lastLine);
  void invalidateLines(qsizetype firstLine, qsizetype lastLine);
  const Block *block(qsizetype index, const Rope &rope);
  QList<QList<HighlightSpan>> computeBlock(qsizetype firstLine, qsizetype lastLine, const Rope &rope);

  TextDocument *m_doc = nullptr;
  std::vector<QMetaObject::Connection> m_connections;

  QString m_languageOverride;
  QString m_fileName;
  std::shared_ptr<const CompiledLanguage> m_lang;

  quint64 m_generation = 1;
  TreePtr m_tree;
  bool m_treeWindowed = false;
  qsizetype m_winStart = 0; // extent of a windowed tree, kept in step with edits
  qsizetype m_winEnd = 0;
  std::vector<InjectedLayer> m_layers;
  bool m_injValid = false;
  qsizetype m_injStart = 0;
  qsizetype m_injEnd = 0;
  std::vector<LoggedEdit> m_editLog; // edits since the last installed result's snapshot

  bool m_running = false;
  bool m_runningWindowed = false;
  bool m_dirty = false;
  bool m_jobScopeChanged = false;
  bool m_needWindow = false;
  qsizetype m_center = 0; // last line asked for: where windows and injection regions are centred
  std::shared_ptr<std::atomic_bool> m_cancel;
  std::shared_ptr<HighlightMailbox> m_mailbox;

  qsizetype m_fullLimit = 16 * 1024 * 1024;
  qsizetype m_windowCap = 2'000'000;

  QCache<qsizetype, Block> m_blocks{512};
  TSQueryCursor *m_cursor = nullptr;
  Stats m_stats;
};

} // namespace qce

#endif // QCE_TREESITTERHIGHLIGHTER_H
