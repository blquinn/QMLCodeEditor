#ifndef QCE_LANGUAGEREGISTRY_H
#define QCE_LANGUAGEREGISTRY_H

#include "syntax/queryinfo.h"

#include <QtCore/QList>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <deque>
#include <memory>

#include <tree_sitter/api.h>

namespace qce {

// A language the editor can highlight (SYNTAX-02): how to recognise it, its grammar, and the query
// files (resource paths, concatenated in order) that style it.
struct LanguageInfo {
  QString id;   // "cpp"
  QString name; // "C++"
  QStringList extensions;   // lower case, without the dot
  QStringList fileNames;    // exact names ("CMakeLists.txt"), lower case
  QStringList interpreters; // shebang interpreters ("python3")
  QStringList aliases;      // names used in Markdown fences and by hosts ("c++", "js")
  const TSLanguage *(*grammar)() = nullptr;
  // Query files, concatenated in order: a path relative to the built-in queries (":/qce/syntax/<path>"), or
  // an absolute one (starting with ":" for a Qt resource or "/" for a file) for a host's own.
  QStringList highlightQueries;
  QStringList injectionQueries;
  QStringList foldQueries; // captures @fold: nodes that can be folded (FOLD-02)
  // Query source given inline, appended after the files above, for hosts that don't ship queries as resources.
  QString highlightSource;
  QString injectionSource;
  QString foldSource;
  bool selectable = true; // false for languages only reached through injection
};

// A language with its queries compiled. Immutable and shared: TSQuery may be used from any thread
// as long as every thread has its own TSQueryCursor.
struct CompiledLanguage {
  const LanguageInfo *info = nullptr;
  const TSLanguage *language = nullptr;
  TSQuery *highlights = nullptr; // null when there are no (compilable) queries
  TSQuery *injections = nullptr;
  TSQuery *folds = nullptr;
  QueryInfo highlightInfo;  // capture styles and predicates, valid when `highlights` is
  QueryInfo injectionInfo;
  QueryInfo foldInfo;
  QStringList warnings; // patterns that had to be dropped, with the reason

  CompiledLanguage() = default;
  CompiledLanguage(const CompiledLanguage &) = delete;
  CompiledLanguage &operator=(const CompiledLanguage &) = delete;
  ~CompiledLanguage();
};

// Compiles query source against a grammar. A pattern that doesn't compile (a node or field the
// pinned grammar doesn't have) is dropped and reported in `warnings` instead of failing the lot.
// Returns null when nothing is left.
TSQuery *compileQuery(const TSLanguage *language, const QString &source, QStringList *warnings);

// The languages the editor knows: the built-in ones, plus any a host registers (API-12).
//
// Registration is for startup: call registerLanguage() on the GUI thread before editors parse (before the
// first SyntaxHighlighter gets text). The registry is then read without locks, including by the parse
// workers (injections look languages up), so changing the list while a parse runs is not allowed; the
// only thing that is synchronized is the cache of compiled queries. A SyntaxHighlighter resolves its
// language again whenever `language` or `fileName` changes, so a language registered after an editor exists
// applies from then on. Languages are compiled on first use; a registered one that is never used costs
// nothing.
class LanguageRegistry {
public:
  static const LanguageRegistry &instance();

  // Adds a language, or replaces the one with the same id (a built-in can be overridden, none can be
  // removed). New languages are tried before the built-ins by detect(), so a host's mapping of an
  // extension wins. The id and aliases are lower-cased; the grammar must be a tree-sitter language of an
  // ABI the vendored runtime supports. Returns false, with the reason in `*error`, if the language is
  // rejected. Replacing drops the compiled queries of the old definition; editors pick up the new one when
  // their language is resolved again.
  static bool registerLanguage(LanguageInfo info, QString *error = nullptr);

  // Every language, hosts' first. Entries keep their address when languages are added (a replaced one is
  // overwritten in place), which `CompiledLanguage::info` and the editors rely on.
  const std::deque<LanguageInfo> &languages() const { return m_languages; }
  // By id or alias, case-insensitive; null if unknown.
  const LanguageInfo *find(const QString &idOrAlias) const;
  // By exact file name, then extension, then the shebang in `firstLine`. `fileName` may be a path.
  const LanguageInfo *detect(const QString &fileName, const QString &firstLine = {}) const;
  // Compiles on first use and caches; thread-safe. Null for unknown ids.
  std::shared_ptr<const CompiledLanguage> compiled(const QString &id) const;

private:
  LanguageRegistry();
  std::deque<LanguageInfo> m_languages;
};

} // namespace qce

#endif // QCE_LANGUAGEREGISTRY_H
