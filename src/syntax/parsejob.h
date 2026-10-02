#ifndef QCE_PARSEJOB_H
#define QCE_PARSEJOB_H

#include "core/textsnapshot.h"
#include "syntax/languageregistry.h"
#include "syntax/ropeinput.h"

#include <QtCore/QList>

#include <atomic>
#include <memory>
#include <vector>

namespace qce {

// A tree for text embedded in another language (SYNTAX-08), in document coordinates: it was
// parsed over included ranges of the host's text. [start, end) bounds those ranges in UTF-16 units.
struct InjectedLayer {
  std::shared_ptr<const CompiledLanguage> language;
  TreePtr tree;
  qsizetype start = 0;
  qsizetype end = 0;
};

// One unit of background work: parse `snapshot` (all of it, or a window), reusing `oldTree`, then
// parse the injections found in [injectionStart, injectionEnd).
struct ParseRequest {
  quint64 generation = 0;
  TextSnapshot snapshot;
  std::shared_ptr<const CompiledLanguage> language;
  TreePtr oldTree; // a copy, already edited to match `snapshot`; may be null
  bool windowed = false;
  qsizetype start = 0; // parse extent in UTF-16 units when windowed
  qsizetype end = 0;
  qsizetype injectionStart = 0;
  qsizetype injectionEnd = 0;
  std::shared_ptr<std::atomic_bool> cancel;
};

struct ParseResult {
  quint64 generation = 0;
  quint64 version = 0;
  bool cancelled = false;
  TreePtr tree;
  bool windowed = false;
  qsizetype start = 0;
  qsizetype end = 0;
  std::vector<InjectedLayer> layers;
  qsizetype injectionStart = 0;
  qsizetype injectionEnd = 0;
  qint64 parseNs = 0;
  qint64 injectionNs = 0;
};

// Runs on any thread. A cancelled parse returns `cancelled` with no tree.
ParseResult runParse(ParseRequest request);

// Maximum nesting of injections (Markdown -> fenced C++ -> a language embedded there).
constexpr int MaxInjectionDepth = 3;

} // namespace qce

#endif // QCE_PARSEJOB_H
