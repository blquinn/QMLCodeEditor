#include "core/findreplace.h"

#include <QtCore/QPromise>
#include <QtCore/QThreadPool>

#include <algorithm>
#include <limits>

namespace qce {

namespace {

constexpr int kDebounceMs = 30; // text changes are searched again after this quiet time
constexpr qsizetype kMaxSeed = 1000;

// The first match starting at or after `offset`, wrapping; -1 for an empty list.
qsizetype firstFrom(const QList<Selection> &matches, qsizetype offset) {
  if (matches.isEmpty())
    return -1;
  const auto it = std::lower_bound(matches.begin(), matches.end(), offset, [](const Selection &m, qsizetype o) {
    return m.start() < o;
  });
  return it == matches.end() ? 0 : it - matches.begin();
}

// The last match starting before `offset`, wrapping; -1 for an empty list.
qsizetype lastBefore(const QList<Selection> &matches, qsizetype offset) {
  if (matches.isEmpty())
    return -1;
  const auto it = std::lower_bound(matches.begin(), matches.end(), offset, [](const Selection &m, qsizetype o) {
    return m.start() < o;
  });
  return it == matches.begin() ? matches.size() - 1 : (it - matches.begin()) - 1;
}

} // namespace

FindReplace::FindReplace(TextDocument &document, SelectionSet &selections, ContextProvider context, QObject *parent)
    : QObject(parent), m_document(document), m_selections(selections), m_context(std::move(context)) {
  m_debounce.setSingleShot(true);
  m_debounce.setInterval(kDebounceMs);
  connect(&m_debounce, &QTimer::timeout, this, [this] { startSearch(); });
  connect(&m_searchWatcher, &QFutureWatcher<SearchResult>::finished, this, &FindReplace::searchFinished);
  connect(&m_replaceWatcher, &QFutureWatcher<ReplaceResult>::finished, this, &FindReplace::replaceFinished);
  connect(&m_document, &TextDocument::changed, this, &FindReplace::documentChanged);
  connect(&m_document, &TextDocument::textReset, this, &FindReplace::documentChanged);
  connect(&m_selections, &SelectionSet::changed, this, &FindReplace::selectionChanged);
  m_query.caseSensitive = false;
}

FindReplace::~FindReplace() {
  cancelJobs();
  m_searchWatcher.waitForFinished();
  m_replaceWatcher.waitForFinished();
}

QRegularExpression FindReplace::highlightPattern() const {
  return m_active && m_pattern.valid() ? m_pattern.regex : QRegularExpression();
}

// --- query

void FindReplace::setActive(bool active) {
  if (active == m_active)
    return;
  m_active = active;
  emit activeChanged();
  if (active) {
    m_origin = m_selections.primary().start();
    m_pendingJump = true;
    startSearch();
  } else {
    cancelJobs();
    m_pendingJump = false;
    clearResults();
  }
  emit highlightChanged();
}

void FindReplace::setText(const QString &text) {
  if (text == m_text)
    return;
  m_text = text;
  emit textChanged();
  queryChanged();
}

void FindReplace::setReplacement(const QString &replacement) {
  if (replacement == m_replacement)
    return;
  m_replacement = replacement;
  emit replacementChanged();
}

void FindReplace::setRegex(bool on) {
  if (on == m_query.regex)
    return;
  m_query.regex = on;
  emit optionsChanged();
  queryChanged();
}

void FindReplace::setCaseSensitive(bool on) {
  if (on == m_query.caseSensitive)
    return;
  m_query.caseSensitive = on;
  emit optionsChanged();
  queryChanged();
}

void FindReplace::setWholeWord(bool on) {
  if (on == m_query.wholeWord)
    return;
  m_query.wholeWord = on;
  emit optionsChanged();
  queryChanged();
}

void FindReplace::queryChanged() {
  const QString before = m_pattern.error;
  m_pattern = search::compileQuery(m_text, m_query);
  if (before != m_pattern.error)
    emit errorChanged();
  cancelJobs(); // a replace-all of the old query must not land
  m_pendingJump = true;
  if (m_active)
    startSearch();
  emit highlightChanged();
}

// --- searching

void FindReplace::cancelJobs() {
  m_debounce.stop();
  if (m_cancel)
    m_cancel->store(true);
  m_cancel.reset();
  if (m_replaceCancel)
    m_replaceCancel->store(true);
  m_replaceCancel.reset();
  ++m_generation;
  ++m_replaceGeneration;
  setBusyFlags(false, false);
}

void FindReplace::setBusyFlags(bool searching, bool replacing) {
  const bool was = busy();
  m_searching = searching;
  m_replacing = replacing;
  if (was != busy())
    emit busyChanged();
}

void FindReplace::clearResults() {
  const bool changed = !m_matches.isEmpty() || m_capped || m_current != -1;
  m_matches.clear();
  m_capped = false;
  m_current = -1;
  m_hasMatches = false;
  if (changed)
    emit resultsChanged();
}

void FindReplace::startSearch() {
  m_debounce.stop();
  if (m_cancel)
    m_cancel->store(true);
  m_cancel = std::make_shared<std::atomic_bool>(false);
  const quint64 generation = ++m_generation;
  if (!m_active || !m_pattern.valid()) {
    setBusyFlags(false, m_replacing);
    clearResults();
    return;
  }
  setBusyFlags(true, m_replacing);

  auto promise = std::make_shared<QPromise<SearchResult>>();
  m_searchWatcher.setFuture(promise->future());
  promise->start();
  const TextSnapshot snapshot = m_document.snapshot();
  const search::Pattern pattern = m_pattern;
  const std::shared_ptr<std::atomic_bool> cancel = m_cancel;
  QThreadPool::globalInstance()->start([=] {
    SearchResult result;
    result.generation = generation;
    result.version = snapshot.version();
    const Rope &rope = snapshot.rope();
    bool capped = false;
    result.matches = search::findAll(rope, pattern, 0, rope.length(), kMaxMatches, &capped, cancel.get());
    result.capped = capped;
    if (!cancel->load())
      promise->addResult(std::move(result));
    promise->finish();
  });
}

void FindReplace::searchFinished() {
  if (m_searchWatcher.future().resultCount() == 0)
    return; // cancelled
  SearchResult result = m_searchWatcher.result();
  if (result.generation != m_generation)
    return;
  setBusyFlags(false, m_replacing);
  m_matches = std::move(result.matches);
  m_capped = result.capped;
  m_matchesVersion = result.version;
  m_hasMatches = true;
  if (result.version != m_document.version()) {
    m_debounce.start(); // the text moved on while searching
  } else if (m_pendingJump) {
    m_pendingJump = false;
    jumpFromOrigin();
  }
  updateCurrent();
  emit resultsChanged();
}

void FindReplace::documentChanged() {
  if (m_active && m_pattern.valid())
    m_debounce.start();
}

void FindReplace::selectionChanged() {
  if (m_ownSelection)
    return;
  m_origin = m_selections.primary().start();
  updateCurrent();
}

void FindReplace::updateCurrent() {
  int index = -1;
  const Selection primary = m_selections.primary();
  if (fresh() && !primary.isEmpty()) {
    const auto it = std::lower_bound(m_matches.begin(), m_matches.end(), primary.start(), [](const Selection &m, qsizetype o) {
      return m.start() < o;
    });
    if (it != m_matches.end() && it->start() == primary.start() && it->end() == primary.end())
      index = int(it - m_matches.begin());
  }
  if (index != m_current) {
    m_current = index;
    emit resultsChanged();
  }
}

// --- moving

void FindReplace::selectMatch(const Selection &match) {
  m_ownSelection = true;
  m_selections.setSingle(match.start(), match.end());
  m_ownSelection = false;
  m_origin = match.start();
  updateCurrent();
  emit selectionMoved();
  emit revealRequested();
}

bool FindReplace::jumpFromOrigin() {
  std::optional<Selection> match;
  if (fresh()) {
    if (m_matches.isEmpty())
      return false;
    // A capped list may end before the origin; the text beyond it is searched directly.
    if (m_capped && m_matches.last().start() < m_origin)
      match = search::find(m_document.rope(), m_pattern, m_origin, true, true, true);
    else
      match = m_matches[firstFrom(m_matches, m_origin)];
  } else {
    match = search::find(m_document.rope(), m_pattern, m_origin, true, true, true);
  }
  if (!match)
    return false;
  selectMatch(*match);
  return true;
}

bool FindReplace::step(bool forward) {
  if (!m_pattern.valid())
    return false;
  const Rope &rope = m_document.rope();
  const Selection primary = m_selections.primary();
  std::optional<Selection> match;
  if (fresh() && !m_capped) {
    if (m_matches.isEmpty())
      return false;
    const qsizetype i = forward ? firstFrom(m_matches, primary.end()) : lastBefore(m_matches, primary.start());
    match = m_matches[i];
  } else {
    match = forward ? search::find(rope, m_pattern, primary.end(), true, true, true)
                    : search::find(rope, m_pattern, primary.start(), false, true, false);
    // An empty match where the cursor is would never move on.
    if (match && match->isEmpty() && match->start() == primary.end() && forward)
      match = search::find(rope, m_pattern, primary.end(), true, true, false);
  }
  if (!match)
    return false;
  selectMatch(*match);
  return true;
}

// --- replacing

std::optional<QString> FindReplace::replacementAt(const Selection &match) const {
  if (!m_pattern.valid())
    return std::nullopt;
  const Rope &rope = m_document.rope();
  const qsizetype line = rope.lineAt(match.start());
  const qsizetype base = rope.lineStart(line);
  if (match.end() > rope.lineEnd(line) && !m_pattern.literal.isEmpty()) {
    // A plain-text needle with a line break in it: compare the text.
    const QString selected = rope.toString(match.start(), match.end());
    if (selected.compare(m_pattern.literal, m_pattern.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive) == 0)
      return m_replacement;
    return std::nullopt;
  }
  const QString text = rope.toString(base, rope.lineEnd(line));
  const QRegularExpressionMatch m = m_pattern.regex.match(text, match.start() - base);
  if (!m.hasMatch() || m.capturedStart() != match.start() - base || m.capturedEnd() != match.end() - base)
    return std::nullopt;
  return m_query.regex ? search::expandReplacement(m_replacement, m) : m_replacement;
}

bool FindReplace::replace() {
  EditContext ctx = m_context();
  if (ctx.settings.readOnly || m_document.isLoading() || !m_pattern.valid())
    return false;
  const Selection primary = m_selections.primary();
  const std::optional<QString> text = replacementAt(primary);
  if (!text)
    return step(true);
  m_ownSelection = true;
  commands::applyReplacements(ctx, {{primary.start(), primary.end(), *text}}, EditKind::Other);
  m_ownSelection = false;
  m_origin = m_selections.primary().head;
  emit selectionMoved();
  step(true);
  return true;
}

void FindReplace::replaceAll() {
  const EditContext ctx = m_context();
  if (ctx.settings.readOnly || m_document.isLoading() || !m_pattern.valid())
    return;
  if (m_replaceCancel)
    m_replaceCancel->store(true);
  m_replaceCancel = std::make_shared<std::atomic_bool>(false);
  const quint64 generation = ++m_replaceGeneration;
  setBusyFlags(m_searching, true);

  auto promise = std::make_shared<QPromise<ReplaceResult>>();
  m_replaceWatcher.setFuture(promise->future());
  promise->start();
  const TextSnapshot snapshot = m_document.snapshot();
  const search::Pattern pattern = m_pattern;
  const QString replacement = m_replacement;
  const bool regexMode = m_query.regex;
  const std::shared_ptr<std::atomic_bool> cancel = m_replaceCancel;
  QThreadPool::globalInstance()->start([=] {
    ReplaceResult result;
    result.generation = generation;
    result.version = snapshot.version();
    const Rope &rope = snapshot.rope();
    if (!regexMode) {
      const QList<Selection> all = search::findAll(
        rope, pattern, 0, rope.length(), std::numeric_limits<qsizetype>::max(), nullptr, cancel.get()
      );
      result.edits.reserve(all.size());
      for (const Selection &m : all)
        result.edits.append({m.start(), m.end(), replacement});
    } else {
      search::forEachLineMatch(
        rope, pattern.regex, 0, rope.lineCount() - 1,
        [&](qsizetype base, const QRegularExpressionMatch &m) {
          result.edits.append({base + m.capturedStart(), base + m.capturedEnd(), search::expandReplacement(replacement, m)});
          return true;
        },
        true, cancel.get()
      );
    }
    result.cancelled = cancel->load();
    promise->addResult(std::move(result));
    promise->finish();
  });
}

void FindReplace::replaceFinished() {
  if (m_replaceWatcher.future().resultCount() == 0)
    return;
  ReplaceResult result = m_replaceWatcher.result();
  if (result.cancelled || result.generation != m_replaceGeneration)
    return;
  setBusyFlags(m_searching, false);
  if (result.version != m_document.version()) {
    replaceAll(); // typed into meanwhile: build the edits again on the new text
    return;
  }
  applyEdits(result.edits);
  if (m_active)
    startSearch();
}

// One undo step. The cursor ends after the last replacement; applyReplacements would leave a
// selection behind every replacement, which is a lot of anchors for a replace-all.
void FindReplace::applyEdits(const QList<commands::Replacement> &edits) {
  const EditContext ctx = m_context();
  if (edits.isEmpty() || ctx.settings.readOnly || m_document.isLoading())
    return;
  const SelectionList before = m_selections.selections();
  qsizetype shift = 0;
  for (qsizetype i = 0; i + 1 < edits.size(); ++i)
    shift += edits[i].insertedLength() - (edits[i].end - edits[i].start);
  const qsizetype cursor = edits.last().start + shift + edits.last().insertedLength();

  m_ownSelection = true;
  {
    SelectionSet::Batch batch(m_selections);
    m_document.beginEditGroup(before);
    for (qsizetype i = edits.size() - 1; i >= 0; --i)
      m_document.replace(edits[i].start, edits[i].end, edits[i].text);
    m_document.endEditGroup({{cursor, cursor}}, EditKind::Other);
    m_selections.setSingle(cursor);
  }
  m_ownSelection = false;
  m_origin = cursor;
  emit selectionMoved();
}

bool FindReplace::selectAllMatches() {
  if (!m_pattern.valid())
    return false;
  const EditContext ctx = m_context();
  const int limit = qMax(1, ctx.settings.maxSelections);
  QList<Selection> list;
  if (fresh() && !m_capped) {
    list = m_matches.size() > limit ? m_matches.first(limit) : m_matches;
  } else {
    const Rope &rope = m_document.rope();
    list = search::findAll(rope, m_pattern, 0, rope.length(), limit);
  }
  if (list.isEmpty())
    return false;
  const qsizetype primary = firstFrom(list, m_selections.primary().start());
  m_document.breakUndoCoalescing();
  m_ownSelection = true;
  m_selections.set(list, int(primary));
  m_ownSelection = false;
  m_origin = m_selections.primary().start();
  updateCurrent();
  emit selectionMoved();
  emit revealRequested();
  return true;
}

void FindReplace::useSelection() {
  const Rope &rope = m_document.rope();
  const Selection primary = m_selections.primary();
  QString seed;
  if (!primary.isEmpty()) {
    if (primary.end() - primary.start() <= kMaxSeed) {
      seed = rope.toString(primary.start(), primary.end());
      if (seed.contains(u'\n') || seed.contains(u'\r'))
        seed.clear();
    }
  } else {
    const qsizetype line = rope.lineAt(primary.head);
    qsizetype a = primary.head, b = primary.head;
    const qsizetype start = rope.lineStart(line), end = rope.lineEnd(line);
    while (a > start && search::isWordChar(rope.at(a - 1)))
      --a;
    while (b < end && search::isWordChar(rope.at(b)))
      ++b;
    seed = rope.toString(a, b);
  }
  if (seed.isEmpty())
    return;
  if (m_query.regex)
    seed = QRegularExpression::escape(seed);
  setText(seed);
}

} // namespace qce
