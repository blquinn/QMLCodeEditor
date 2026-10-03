#ifndef QCE_LANGUAGEREGISTRY_H
#define QCE_LANGUAGEREGISTRY_H

#include "syntax/queryinfo.h"

#include <QtCore/QList>
#include <QtCore/QString>
#include <QtCore/QStringList>

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
  QStringList highlightQueries;
  QStringList injectionQueries;
  QStringList foldQueries; // captures @fold: nodes that can be folded (FOLD-02)
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

class LanguageRegistry {
public:
  static const LanguageRegistry &instance();

  const QList<LanguageInfo> &languages() const { return m_languages; }
  // By id or alias, case-insensitive; null if unknown.
  const LanguageInfo *find(const QString &idOrAlias) const;
  // By exact file name, then extension, then the shebang in `firstLine`. `fileName` may be a path.
  const LanguageInfo *detect(const QString &fileName, const QString &firstLine = {}) const;
  // Compiles on first use and caches; thread-safe. Null for unknown ids.
  std::shared_ptr<const CompiledLanguage> compiled(const QString &id) const;

private:
  LanguageRegistry();
  QList<LanguageInfo> m_languages;
};

} // namespace qce

#endif // QCE_LANGUAGEREGISTRY_H
