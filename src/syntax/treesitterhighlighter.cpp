#include "syntax/treesitterhighlighter.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QElapsedTimer>
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QThreadPool>

#include <algorithm>
#include <map>

using namespace Qt::StringLiterals;

namespace qce {

// Results travel from workers to the GUI thread through this: the worker queues a result and posts
// a functor to the application object; the functor drains the queue into the highlighter if it is
// still alive. Only the GUI thread touches `owner`, so a highlighter can be destroyed while a
// parse is running without waiting for it.
struct HighlightMailbox {
  QMutex mutex;
  TreeSitterHighlighter *owner = nullptr;
  std::vector<std::unique_ptr<ParseResult>> results;

  void drain() {
    std::vector<std::unique_ptr<ParseResult>> taken;
    {
      QMutexLocker lock(&mutex);
      taken.swap(results);
    }
    for (auto &result : taken)
      if (owner)
        owner->applyResult(std::move(*result));
  }
};

namespace {

constexpr qsizetype InjectionWholeDocument = 300'000; // documents up to this many units get injections everywhere
constexpr qsizetype InjectionMarginUnits = 4000;
constexpr qsizetype WindowMarginLines = 1000;
constexpr qsizetype InjectionLines = 300;

// Where an offset goes when [start, oldEnd) is replaced by text ending at newEnd.
qsizetype adjustOffset(qsizetype offset, const TSInputEdit &e) {
  const qsizetype start = toUnit(e.start_byte), oldEnd = toUnit(e.old_end_byte), newEnd = toUnit(e.new_end_byte);
  if (offset >= oldEnd)
    return offset + (newEnd - oldEnd);
  if (offset > start)
    return start;
  return offset;
}

qsizetype adjustLine(qsizetype line, qsizetype startLine, qsizetype oldEndLine, qsizetype newEndLine) {
  if (line > oldEndLine)
    return line + (newEndLine - oldEndLine);
  if (line > startLine)
    return qMin(line, newEndLine);
  return line;
}

struct Segment {
  qsizetype from;
  qsizetype to;
  TokenStyle style;
};

struct Capture {
  qsizetype start;
  qsizetype end;
  uint8_t depth; // dotted segments of the capture name: a more specific name wins over a generic one
  uint32_t pattern;
  TokenStyle style;
};

// Styled, non-overlapping segments of [from, to) from running a highlight query on `root`. Nested
// captures override the ones around them. For identical ranges the more specific capture name wins
// (@string.special.key over @string), then the later pattern: the upstream queries are written for
// both conventions (C lists `(identifier) @variable` first, JSON lists its key pattern first).
std::vector<Segment> paintTree(
  TSQueryCursor *cursor, TSNode root, const CompiledLanguage &language, qsizetype from, qsizetype to, const Rope &rope
) {
  std::vector<Segment> out;
  if (!language.highlights)
    return out;
  const QueryInfo &info = language.highlightInfo;
  ts_query_cursor_set_byte_range(cursor, toByte(from), toByte(to));
  ts_query_cursor_exec(cursor, language.highlights, root);

  std::vector<Capture> captures;
  TSQueryMatch match;
  uint32_t index = 0;
  while (ts_query_cursor_next_capture(cursor, &match, &index)) {
    const TSQueryCapture &capture = match.captures[index];
    const auto &style = info.captureStyles[capture.index];
    if (!style)
      continue;
    if (!info.patterns[match.pattern_index].predicates.empty() && !predicatesHold(info, match, rope)) {
      ts_query_cursor_remove_match(cursor, match.id);
      continue;
    }
    const qsizetype start = qMax(toUnit(ts_node_start_byte(capture.node)), from);
    const qsizetype end = qMin(toUnit(ts_node_end_byte(capture.node)), to);
    if (end > start)
      captures.push_back({start, end, info.captureDepth[capture.index], match.pattern_index, *style});
  }
  std::sort(captures.begin(), captures.end(), [](const Capture &a, const Capture &b) {
    if (a.start != b.start)
      return a.start < b.start;
    if (a.end != b.end)
      return a.end > b.end;
    if (a.depth != b.depth)
      return a.depth < b.depth;
    return a.pattern < b.pattern; // painted last, so the later pattern wins
  });

  auto put = [&out](qsizetype a, qsizetype b, TokenStyle style) {
    if (b <= a || style == TokenStyle::Default)
      return;
    if (!out.empty() && out.back().to == a && out.back().style == style)
      out.back().to = b;
    else
      out.push_back({a, b, style});
  };
  std::vector<Capture *> stack;
  qsizetype pos = from;
  for (Capture &capture : captures) {
    while (!stack.empty() && stack.back()->end <= capture.start) {
      put(pos, stack.back()->end, stack.back()->style);
      pos = stack.back()->end;
      stack.pop_back();
    }
    if (!stack.empty()) {
      put(pos, capture.start, stack.back()->style);
      capture.end = qMin(capture.end, stack.back()->end);
    }
    pos = capture.start;
    stack.push_back(&capture);
  }
  while (!stack.empty()) {
    put(pos, stack.back()->end, stack.back()->style);
    pos = stack.back()->end;
    stack.pop_back();
  }
  return out;
}

// `top` painted over `base`; both sorted and non-overlapping.
QList<HighlightSpan> overlay(const QList<HighlightSpan> &base, const QList<HighlightSpan> &top) {
  if (top.isEmpty())
    return base;
  if (base.isEmpty())
    return top;
  QList<HighlightSpan> out;
  out.reserve(base.size() + top.size());
  for (const HighlightSpan &b : base) {
    qsizetype s = b.start;
    const qsizetype e = b.start + b.length;
    for (const HighlightSpan &t : top) {
      const qsizetype ts = t.start, te = t.start + t.length;
      if (te <= s)
        continue;
      if (ts >= e)
        break;
      if (ts > s)
        out.append({s, ts - s, b.style});
      s = qMax(s, te);
      if (s >= e)
        break;
    }
    if (s < e)
      out.append({s, e - s, b.style});
  }
  out += top;
  std::sort(out.begin(), out.end(), [](const HighlightSpan &a, const HighlightSpan &b) { return a.start < b.start; });
  return out;
}

} // namespace

TreeSitterHighlighter::TreeSitterHighlighter(QObject *parent)
    : Highlighter(parent), m_mailbox(std::make_shared<HighlightMailbox>()) {
  m_mailbox->owner = this;
}

TreeSitterHighlighter::~TreeSitterHighlighter() {
  detach();
  {
    QMutexLocker lock(&m_mailbox->mutex);
    m_mailbox->owner = nullptr;
  }
  if (m_cursor)
    ts_query_cursor_delete(m_cursor);
}

QStringList TreeSitterHighlighter::availableLanguages() {
  QStringList ids;
  for (const LanguageInfo &info : LanguageRegistry::instance().languages())
    if (info.selectable)
      ids.append(info.id);
  return ids;
}

QString TreeSitterHighlighter::languageName(const QString &id) {
  const LanguageInfo *info = LanguageRegistry::instance().find(id);
  return info ? info->name : QString();
}

void TreeSitterHighlighter::setLanguage(const QString &language) {
  if (language == m_languageOverride)
    return;
  m_languageOverride = language;
  emit languageChanged();
  resetState();
}

void TreeSitterHighlighter::setFileName(const QString &fileName) {
  if (fileName == m_fileName)
    return;
  m_fileName = fileName;
  emit fileNameChanged();
  if (m_languageOverride.isEmpty())
    resetState();
}

void TreeSitterHighlighter::setFullParseLimit(qsizetype units) {
  if (units == m_fullLimit)
    return;
  m_fullLimit = units;
  emit fullParseLimitChanged();
  if (m_doc && m_lang)
    scheduleParse(); // a window tree may now be due a full parse (or the other way round)
}

void TreeSitterHighlighter::setParseMemoryBudget(qint64 bytes) {
  bytes = qMax<qint64>(bytes, 0);
  if (bytes == m_parseBudget)
    return;
  m_parseBudget = bytes;
  emit parseMemoryBudgetChanged();
  if (m_doc && m_lang)
    scheduleParse();
}

qsizetype TreeSitterHighlighter::effectiveFullParseLimit() const {
  if (m_parseBudget <= 0)
    return m_fullLimit;
  constexpr int kUnknownBytesPerUnit = 64; // above every measured built-in except Markdown
  const int perUnit = m_lang && m_lang->info && m_lang->info->treeBytesPerUnit > 0 ? m_lang->info->treeBytesPerUnit
                                                                                   : kUnknownBytesPerUnit;
  return qMin<qsizetype>(m_fullLimit, qsizetype(m_parseBudget / perUnit));
}

void TreeSitterHighlighter::setWindowSize(qsizetype units) {
  m_windowCap = qMax<qsizetype>(units, 64);
  if (m_doc && m_lang)
    resetState();
}

std::pair<qsizetype, qsizetype> TreeSitterHighlighter::parsedRange() const {
  if (!m_tree)
    return {0, 0};
  if (m_treeWindowed)
    return {m_winStart, m_winEnd};
  return {0, m_doc ? m_doc->length() : 0};
}

QString TreeSitterHighlighter::debugTree() const {
  if (!m_tree)
    return {};
  char *s = ts_node_string(ts_tree_root_node(m_tree.get()));
  const QString out = QString::fromUtf8(s);
  free(s);
  return out;
}

void TreeSitterHighlighter::attach(TextDocument *document) {
  if (document == m_doc)
    return;
  detach();
  m_doc = document;
  if (!m_doc)
    return;
  m_connections.push_back(connect(m_doc, &TextDocument::changed, this, &TreeSitterHighlighter::onDocumentChanged));
  m_connections.push_back(connect(m_doc, &TextDocument::textReset, this, &TreeSitterHighlighter::onDocumentReset));
  m_connections.push_back(connect(m_doc, &TextDocument::loadFinished, this, &TreeSitterHighlighter::onLoadFinished));
  m_connections.push_back(connect(m_doc, &QObject::destroyed, this, [this] { m_doc = nullptr; }));
  resetState();
}

void TreeSitterHighlighter::detach() {
  for (const QMetaObject::Connection &c : m_connections)
    disconnect(c);
  m_connections.clear();
  const bool had = m_doc != nullptr;
  m_doc = nullptr;
  if (m_cancel)
    m_cancel->store(true);
  ++m_generation;
  if (had)
    dropTrees();
}

void TreeSitterHighlighter::dropTrees() {
  m_tree.reset();
  m_layers.clear();
  m_treeWindowed = false;
  m_injValid = false;
  m_editLog.clear();
  m_blocks.clear();
  m_needWindow = false;
  m_dirty = false;
  m_stats.haveTree = m_stats.haveFullTree = false;
}

void TreeSitterHighlighter::redetectLanguage() {
  const LanguageRegistry &registry = LanguageRegistry::instance();
  const LanguageInfo *info = nullptr;
  if (!m_languageOverride.isEmpty()) {
    info = registry.find(m_languageOverride);
  } else {
    QString firstLine;
    if (m_doc && m_doc->length() > 0)
      firstLine = m_doc->rope().toString(0, qMin<qsizetype>(m_doc->rope().lineEnd(0), 200));
    info = registry.detect(m_fileName, firstLine);
  }
  std::shared_ptr<const CompiledLanguage> compiled;
  if (info && info->selectable) {
    compiled = registry.compiled(info->id);
    if (compiled && !compiled->highlights)
      compiled = nullptr;
  }
  const QString before = detectedLanguage();
  m_lang = std::move(compiled);
  if (before != detectedLanguage())
    emit detectedLanguageChanged();
}

void TreeSitterHighlighter::resetState() {
  ++m_generation;
  if (m_cancel)
    m_cancel->store(true);
  const bool hadTree = m_tree != nullptr;
  dropTrees();
  m_center = 0;
  m_centerUnit = 0;
  redetectLanguage();
  if (m_doc && (hadTree || m_lang))
    emit invalidated(AllLines, AllLines);
  scheduleParse();
}

void TreeSitterHighlighter::onDocumentReset() {
  // The text was replaced wholesale (load, setText): nothing about the old trees applies.
  resetState();
}

void TreeSitterHighlighter::onLoadFinished() {
  // The first line is final now (shebang), and a window tree may be due a full parse.
  const QString before = detectedLanguage();
  if (m_languageOverride.isEmpty()) {
    redetectLanguage();
    if (before != detectedLanguage()) {
      resetState();
      return;
    }
  }
  m_dirty = true;
  scheduleParse();
}

void TreeSitterHighlighter::onDocumentChanged(const TextChange &change) {
  if (!m_lang)
    return;
  TSInputEdit edit;
  edit.start_byte = toByte(change.start);
  edit.old_end_byte = toByte(change.oldEnd);
  edit.new_end_byte = toByte(change.newEnd);
  edit.start_point = toPoint(change.startPos);
  edit.old_end_point = toPoint(change.oldEndPos);
  edit.new_end_point = toPoint(change.newEndPos);

  // The trees follow the text at once, so spans asked for before the next parse lands are
  // shifted rather than lost (SYNTAX-09).
  if (m_tree)
    ts_tree_edit(m_tree.get(), &edit);
  for (InjectedLayer &layer : m_layers) {
    ts_tree_edit(layer.tree.get(), &edit);
    layer.start = adjustOffset(layer.start, edit);
    layer.end = adjustOffset(layer.end, edit);
  }
  m_winStart = adjustOffset(m_winStart, edit);
  m_winEnd = adjustOffset(m_winEnd, edit);
  m_injStart = adjustOffset(m_injStart, edit);
  m_injEnd = adjustOffset(m_injEnd, edit);
  m_editLog.push_back(
    {change.versionAfter, edit, change.startPos.line, change.oldEndPos.line, change.newEndPos.line}
  );

  const qsizetype first = change.startPos.line;
  if (change.oldEndPos.line != change.newEndPos.line)
    dropBlocks(first, std::numeric_limits<qsizetype>::max());
  else
    dropBlocks(first, change.oldEndPos.line);
  scheduleParse();
}

bool TreeSitterHighlighter::planWindowed() const {
  const qsizetype length = m_doc->length();
  if (length <= m_windowCap)
    return false;
  if (m_needWindow || m_doc->isLoading())
    return true;
  if (length <= effectiveFullParseLimit())
    return m_tree == nullptr; // a window first so the viewport shows something soon, then the whole text
  return true;
}

void TreeSitterHighlighter::scheduleParse() {
  if (!m_doc || !m_lang)
    return;
  if (m_running) {
    m_dirty = true;
    // A long whole-document parse is not worth waiting for when the viewport has no highlights.
    if (m_needWindow && !m_runningWindowed && m_cancel)
      m_cancel->store(true);
    return;
  }
  startJob();
}

void TreeSitterHighlighter::startJob() {
  auto request = std::make_shared<ParseRequest>();
  request->generation = m_generation;
  request->snapshot = m_doc->snapshot();
  request->language = m_lang;
  request->cancel = m_cancel = std::make_shared<std::atomic_bool>(false);
  if (m_tree)
    request->oldTree.reset(ts_tree_copy(m_tree.get()));
  const Rope &rope = request->snapshot.rope();
  const qsizetype length = rope.length();
  const qsizetype lines = rope.lineCount();
  const qsizetype centerUnit = qBound<qsizetype>(0, m_centerUnit, length);
  const qsizetype center = rope.lineAt(centerUnit);

  bool scopeChanged = !m_tree;
  request->windowed = planWindowed();
  if (request->windowed) {
    const bool keep = m_tree && m_treeWindowed && centerUnit >= m_winStart &&
                      (centerUnit < m_winEnd || m_winEnd >= length) && m_winEnd - m_winStart <= 2 * m_windowCap;
    if (keep) {
      request->start = m_winStart;
      request->end = m_winEnd;
    } else {
      qsizetype start = rope.lineStart(qMax<qsizetype>(0, center - WindowMarginLines));
      qsizetype end = center + WindowMarginLines + 1 < lines ? rope.lineStart(center + WindowMarginLines + 1) : length;
      if (end - start > m_windowCap) {
        // Few, very long lines: cut the window around the viewport even if that splits a line.
        const qsizetype from = qMax(start, centerUnit - m_windowCap / 2);
        const qsizetype lineStart = rope.lineStart(rope.lineAt(from));
        start = from - lineStart <= m_windowCap / 8 ? lineStart : from;
        end = qMin(end, centerUnit + m_windowCap / 2);
        end = qMax(end, qMin(length, start + 1));
      }
      request->start = start;
      request->end = end;
      scopeChanged = true;
    }
  } else {
    request->start = 0;
    request->end = length;
  }
  // This job covers whatever the viewport asked for; leaving the flag set re-queues a parse forever.
  m_needWindow = false;
  if (request->windowed != m_treeWindowed)
    scopeChanged = true;

  if (m_lang->injections) {
    if (length <= InjectionWholeDocument) {
      request->injectionStart = 0;
      request->injectionEnd = length;
      if (!m_injValid || m_injStart != 0 || m_injEnd < length)
        scopeChanged = true;
    } else {
      const bool keep = m_injValid && (centerUnit >= m_injStart + InjectionMarginUnits || m_injStart == 0) &&
                        (centerUnit + InjectionMarginUnits <= m_injEnd || m_injEnd >= length);
      if (keep) {
        request->injectionStart = m_injStart;
        request->injectionEnd = m_injEnd;
      } else {
        request->injectionStart = rope.lineStart(qMax<qsizetype>(0, center - InjectionLines));
        request->injectionEnd =
          center + InjectionLines + 1 < lines ? rope.lineStart(center + InjectionLines + 1) : length;
        scopeChanged = true;
      }
    }
  }

  m_jobScopeChanged = scopeChanged;
  m_running = true;
  m_runningWindowed = request->windowed;
  m_dirty = false;
  ++m_stats.started;
  if (request->windowed)
    ++m_stats.windowParses;
  else
    ++m_stats.fullParses;
  emit parsingChanged();

  const std::shared_ptr<HighlightMailbox> mailbox = m_mailbox;
  QThreadPool::globalInstance()->start([request, mailbox] {
    auto result = std::make_unique<ParseResult>(runParse(std::move(*request)));
    {
      QMutexLocker lock(&mailbox->mutex);
      mailbox->results.push_back(std::move(result));
    }
    if (QCoreApplication *app = QCoreApplication::instance())
      QMetaObject::invokeMethod(app, [mailbox] { mailbox->drain(); }, Qt::QueuedConnection);
  });
}

void TreeSitterHighlighter::applyResult(ParseResult result) {
  m_running = false;
  const bool stale = result.generation != m_generation || !m_doc || !m_lang;
  if (stale || result.cancelled) {
    ++m_stats.discarded;
    emit parsingChanged();
    if (m_doc && m_lang)
      scheduleParse();
    return;
  }

  const quint64 version = result.version;
  const Rope &rope = m_doc->rope();
  const qsizetype length = rope.length();

  // Edits made while the worker was busy: bring the new trees up to date with the document.
  qsizetype newStart = result.start, newEnd = result.end;
  qsizetype injStart = result.injectionStart, injEnd = result.injectionEnd;
  for (const LoggedEdit &e : m_editLog) {
    if (e.version <= version)
      continue;
    ts_tree_edit(result.tree.get(), &e.edit);
    for (InjectedLayer &layer : result.layers) {
      ts_tree_edit(layer.tree.get(), &e.edit);
      layer.start = adjustOffset(layer.start, e.edit);
      layer.end = adjustOffset(layer.end, e.edit);
    }
    newStart = adjustOffset(newStart, e.edit);
    newEnd = adjustOffset(newEnd, e.edit);
    injStart = adjustOffset(injStart, e.edit);
    injEnd = adjustOffset(injEnd, e.edit);
  }

  // Which lines may now look different (SYNTAX-07): what the parse reports as changed, the lines
  // edited since the last result (their spans were computed from an interim tree), and, when the
  // covered region moved, the old and the new region.
  const qsizetype lineCount = rope.lineCount();
  qsizetype lo = std::numeric_limits<qsizetype>::max(), hi = -1;
  auto include = [&](qsizetype a, qsizetype b) {
    lo = qMin(lo, a);
    hi = qMax(hi, b);
  };
  auto includeUnits = [&](qsizetype a, qsizetype b) {
    include(rope.lineAt(qBound<qsizetype>(0, a, length)), rope.lineAt(qBound<qsizetype>(0, b, length)));
  };
  qsizetype dirtyLo = std::numeric_limits<qsizetype>::max(), dirtyHi = -1;
  for (const LoggedEdit &e : m_editLog) {
    if (dirtyHi >= 0) {
      dirtyLo = adjustLine(dirtyLo, e.startLine, e.oldEndLine, e.newEndLine);
      dirtyHi = adjustLine(dirtyHi, e.startLine, e.oldEndLine, e.newEndLine);
    }
    dirtyLo = qMin(dirtyLo, e.startLine);
    dirtyHi = qMax(dirtyHi, e.newEndLine);
  }
  if (dirtyHi >= 0)
    include(dirtyLo, dirtyHi);

  if (m_tree && !m_jobScopeChanged) {
    uint32_t count = 0;
    TSRange *ranges = ts_tree_get_changed_ranges(m_tree.get(), result.tree.get(), &count);
    for (uint32_t i = 0; i < count; ++i) {
      qsizetype a = ranges[i].start_point.row, b = ranges[i].end_point.row;
      for (const LoggedEdit &e : m_editLog) {
        if (e.version <= version)
          continue;
        a = adjustLine(a, e.startLine, e.oldEndLine, e.newEndLine);
        b = adjustLine(b, e.startLine, e.oldEndLine, e.newEndLine);
      }
      include(a, b);
    }
    free(ranges);
  } else {
    if (m_tree) {
      const auto [a, b] = parsedRange();
      includeUnits(a, b);
      if (m_injValid)
        includeUnits(m_injStart, m_injEnd);
    }
    includeUnits(newStart, newEnd);
    if (m_lang->injections)
      includeUnits(injStart, injEnd);
  }

  m_tree = std::move(result.tree);
  m_treeWindowed = result.windowed;
  m_winStart = newStart;
  m_winEnd = newEnd;
  m_layers = std::move(result.layers);
  m_injValid = m_lang->injections != nullptr;
  m_injStart = injStart;
  m_injEnd = injEnd;
  m_editLog.erase(
    std::remove_if(m_editLog.begin(), m_editLog.end(), [version](const LoggedEdit &e) { return e.version <= version; }),
    m_editLog.end()
  );
  ++m_stats.landed;
  m_stats.lastParseNs = result.parseNs;
  m_stats.lastInjectionNs = result.injectionNs;
  m_stats.haveTree = true;
  m_stats.haveFullTree = !m_treeWindowed;

  if (hi >= lo)
    invalidateLines(lo, qMin(hi, lineCount - 1));
  emit parsingChanged();
  emit parseFinished(version);

  if (m_dirty || m_doc->version() != version || m_needWindow || m_treeWindowed != planWindowed())
    scheduleParse();
}

bool TreeSitterHighlighter::covered(qsizetype unit) const {
  if (!m_tree)
    return false;
  if (!m_treeWindowed)
    return true;
  return unit >= m_winStart && (unit < m_winEnd || m_winEnd >= m_doc->length());
}

void TreeSitterHighlighter::dropBlocks(qsizetype firstLine, qsizetype lastLine) {
  if (m_blocks.isEmpty())
    return;
  const QList<qsizetype> keys = m_blocks.keys();
  for (qsizetype key : keys)
    if (key * BlockLines <= lastLine && key * BlockLines + BlockLines - 1 >= firstLine)
      m_blocks.remove(key);
}

void TreeSitterHighlighter::invalidateLines(qsizetype firstLine, qsizetype lastLine) {
  dropBlocks(firstLine, lastLine);
  if (firstLine <= 0 && lastLine >= m_doc->rope().lineCount() - 1)
    emit invalidated(AllLines, AllLines);
  else
    emit invalidated(firstLine, lastLine);
}

QList<QList<HighlightSpan>> TreeSitterHighlighter::computeBlock(qsizetype firstLine, qsizetype lastLine, const Rope &rope) {
  const qsizetype n = lastLine - firstLine + 1;
  QList<QList<HighlightSpan>> lines(n);
  if (!m_cursor)
    m_cursor = ts_query_cursor_new();

  QVarLengthArray<qsizetype, BlockLines + 1> starts(n + 1);
  QVarLengthArray<qsizetype, BlockLines> ends(n);
  for (qsizetype i = 0; i < n; ++i) {
    starts[i] = rope.lineStart(firstLine + i);
    ends[i] = rope.lineEnd(firstLine + i);
  }
  starts[n] = lastLine + 1 < rope.lineCount() ? rope.lineStart(lastLine + 1) : rope.length();
  const qsizetype blockStart = starts[0], blockEnd = starts[n];

  auto spread = [&](const std::vector<Segment> &segments) {
    QList<QList<HighlightSpan>> perLine(n);
    for (const Segment &seg : segments) {
      qsizetype i = std::upper_bound(starts.begin(), starts.begin() + n, seg.from) - starts.begin() - 1;
      for (i = qMax<qsizetype>(i, 0); i < n && starts[i] < seg.to; ++i) {
        const qsizetype a = qMax(seg.from, starts[i]), b = qMin(seg.to, ends[i]);
        if (b > a)
          perLine[i].append({a - starts[i], b - a, seg.style});
      }
    }
    return perLine;
  };

  if (covered(blockStart) || covered(blockEnd - 1))
    lines = spread(paintTree(m_cursor, ts_tree_root_node(m_tree.get()), *m_lang, blockStart, blockEnd, rope));
  for (const InjectedLayer &layer : std::as_const(m_layers)) {
    if (layer.end <= blockStart || layer.start >= blockEnd)
      continue;
    const auto top = spread(paintTree(m_cursor, ts_tree_root_node(layer.tree.get()), *layer.language, blockStart, blockEnd, rope));
    for (qsizetype i = 0; i < n; ++i)
      lines[i] = overlay(lines[i], top[i]);
  }
  return lines;
}

const TreeSitterHighlighter::Block *TreeSitterHighlighter::block(qsizetype index, const Rope &rope) {
  if (Block *cached = m_blocks.object(index))
    return cached;
  const qsizetype first = index * BlockLines;
  const qsizetype last = qMin(first + BlockLines - 1, rope.lineCount() - 1);
  QElapsedTimer timer;
  timer.start();
  auto *created = new Block{computeBlock(first, last, rope)};
  m_stats.lastBlockNs = timer.nsecsElapsed();
  ++m_stats.blocksComputed;
  m_blocks.insert(index, created);
  return created;
}

QList<QList<HighlightSpan>>
TreeSitterHighlighter::highlightLines(const TextSnapshot &text, qsizetype firstLine, qsizetype lastLine) {
  firstLine = qMax<qsizetype>(firstLine, 0);
  lastLine = qMin(lastLine, text.lineCount() - 1);
  QList<QList<HighlightSpan>> result(qMax<qsizetype>(0, lastLine - firstLine + 1));
  if (!m_doc || !m_lang || result.isEmpty())
    return result;
  const Rope &rope = text.rope();

  m_center = lastLine;
  m_centerUnit = rope.lineStart(lastLine);
  const bool inWindow = covered(rope.lineStart(firstLine));
  const bool injected = !m_lang->injections || !m_injValid || m_doc->length() <= InjectionWholeDocument ||
                        (rope.lineStart(firstLine) >= m_injStart && rope.lineStart(firstLine) < m_injEnd) ||
                        m_injEnd >= m_doc->length();
  if (!m_tree || !inWindow || !injected) {
    // Nothing to show here yet: ask for a parse around these lines (a no-op while one is running
    // for them) and leave them plain until it lands.
    if (!inWindow || !injected) {
      m_needWindow = !inWindow;
      m_dirty = true;
    }
    if (!m_tree && !m_running)
      scheduleParse();
    else if (!inWindow || !injected)
      scheduleParse();
    if (!m_tree)
      return result;
  }

  for (qsizetype line = firstLine; line <= lastLine; ++line) {
    const Block *b = block(line / BlockLines, rope);
    result[line - firstLine] = b->lines.value(line % BlockLines);
  }
  return result;
}

QList<HighlightSpan>
TreeSitterHighlighter::highlightRange(const TextSnapshot &text, qsizetype line, qsizetype startColumn, qsizetype endColumn) {
  QList<HighlightSpan> out;
  if (!m_doc || !m_lang || line < 0 || line >= text.lineCount() || endColumn <= startColumn)
    return out;
  const Rope &rope = text.rope();
  const qsizetype lineStart = rope.lineStart(line);
  const qsizetype from = lineStart + startColumn, to = qMin(lineStart + endColumn, rope.length());
  if (to <= from)
    return out;

  // The window of a windowed parse follows the stretch being drawn, not the start of its line.
  m_center = line;
  m_centerUnit = from + (to - from) / 2;
  const bool inWindow = covered(from) || covered(to - 1);
  const bool injected = !m_lang->injections || !m_injValid || m_doc->length() <= InjectionWholeDocument ||
                        (from >= m_injStart && from < m_injEnd) || m_injEnd >= m_doc->length();
  if (!m_tree || !inWindow || !injected) {
    if (!inWindow || !injected) {
      m_needWindow = !inWindow;
      m_dirty = true;
    }
    scheduleParse();
    if (!m_tree)
      return out;
  }
  if (!m_cursor)
    m_cursor = ts_query_cursor_new();

  auto toSpans = [&](const std::vector<Segment> &segments) {
    QList<HighlightSpan> spans;
    spans.reserve(qsizetype(segments.size()));
    for (const Segment &seg : segments) {
      const qsizetype a = qMax(seg.from, from), b = qMin(seg.to, to);
      if (b > a)
        spans.append({a - from, b - a, seg.style});
    }
    return spans;
  };
  // Only what the tree covers: outside a windowed parse there is nothing to query.
  if (inWindow) {
    const qsizetype lo = m_treeWindowed ? qMax(from, m_winStart) : from;
    const qsizetype hi = m_treeWindowed && m_winEnd < m_doc->length() ? qMin(to, m_winEnd) : to;
    if (hi > lo)
      out = toSpans(paintTree(m_cursor, ts_tree_root_node(m_tree.get()), *m_lang, lo, hi, rope));
  }
  for (const InjectedLayer &layer : std::as_const(m_layers)) {
    if (layer.end <= from || layer.start >= to)
      continue;
    out = overlay(out, toSpans(paintTree(m_cursor, ts_tree_root_node(layer.tree.get()), *layer.language, from, to, rope)));
  }
  return out;
}

// ---- Folds (FOLD-02) -------------------------------------------------------------------------

TreeSitterFoldProvider::TreeSitterFoldProvider(TreeSitterHighlighter *owner) : FoldProvider(nullptr), m_owner(owner) {
  connect(owner, &Highlighter::invalidated, this, &FoldProvider::invalidated);
}

QList<FoldRange> TreeSitterFoldProvider::foldRanges(const TextSnapshot &text, qsizetype firstLine, qsizetype lastLine) {
  return m_owner->foldRangesImpl(text, firstLine, lastLine, m_fallback);
}

namespace {

// Adds the ranges of every @fold capture of `query` in `root` whose header line is in
// [firstLine, lastLine] to `out`, keyed by header (the longest range wins).
void collectFolds(
  TSQueryCursor *cursor, TSNode root, const CompiledLanguage &lang, const Rope &rope, qsizetype firstLine,
  qsizetype lastLine, std::map<qsizetype, qsizetype> &out
) {
  const qsizetype from = rope.lineStart(firstLine);
  const qsizetype to = lastLine + 1 < rope.lineCount() ? rope.lineStart(lastLine + 1) : rope.length();
  ts_query_cursor_set_byte_range(cursor, toByte(from), toByte(to));
  // A fold hides lines, so only a node that spans several of them can be one, and so can only the patterns
  // rooted at such nodes (their ancestors span several lines too). Minified code is thousands of nodes on a
  // line; the walk below skips every single-line subtree instead of matching the query against all of it, which
  // cost about 20 ms per 40 KB on every keystroke.
  ts_query_cursor_set_max_start_depth(cursor, 0);
  auto collect = [&](const TSQueryMatch &match) {
    if (!predicatesHold(lang.foldInfo, match, rope))
      return;
    for (uint16_t i = 0; i < match.capture_count; ++i) {
      uint32_t nameLength = 0;
      const char *name = ts_query_capture_name_for_id(lang.folds, match.captures[i].index, &nameLength);
      const QLatin1StringView captured(name, qsizetype(nameLength));
      // @fold hides up to a closing token; @fold.keep_last always leaves the node's last line showing.
      if (captured != "fold"_L1 && captured != "fold.keep_last"_L1)
        continue;
      const TSNode node = match.captures[i].node;
      const qsizetype start = toUnit(ts_node_start_byte(node)), end = toUnit(ts_node_end_byte(node));
      const qsizetype header = rope.lineAt(start);
      if (header < firstLine || header > lastLine)
        continue;
      const TextPosition endPos = rope.positionAt(end);
      if (endPos.line <= header)
        continue;
      const qsizetype endLine = foldEndLine(rope, endPos.line, endPos.column, captured == "fold.keep_last"_L1);
      if (endLine <= header)
        continue;
      auto [it, added] = out.try_emplace(header, endLine);
      if (!added && it->second < endLine)
        it->second = endLine;
    }
  };
  TSTreeCursor walker = ts_tree_cursor_new(root);
  bool done = false;
  while (!done) {
    const TSNode node = ts_tree_cursor_current_node(&walker);
    const bool reaches = toUnit(ts_node_end_byte(node)) > from && toUnit(ts_node_start_byte(node)) < to;
    if (reaches && ts_node_start_point(node).row != ts_node_end_point(node).row) {
      ts_query_cursor_exec(cursor, lang.folds, node);
      TSQueryMatch match;
      while (ts_query_cursor_next_match(cursor, &match))
        collect(match);
      if (ts_tree_cursor_goto_first_child(&walker))
        continue;
    }
    while (!ts_tree_cursor_goto_next_sibling(&walker)) {
      if (!ts_tree_cursor_goto_parent(&walker)) {
        done = true;
        break;
      }
    }
  }
  ts_tree_cursor_delete(&walker);
  ts_query_cursor_set_max_start_depth(cursor, std::numeric_limits<uint32_t>::max());
}

} // namespace

QList<FoldRange> TreeSitterHighlighter::foldRangesImpl(
  const TextSnapshot &text, qsizetype firstLine, qsizetype lastLine, IndentFoldProvider &fallback
) {
  firstLine = qMax<qsizetype>(0, firstLine);
  lastLine = qMin(lastLine, text.lineCount() - 1);
  if (firstLine > lastLine)
    return {};
  if (!m_doc || !m_tree || !m_lang || !m_lang->folds)
    return fallback.foldRanges(text, firstLine, lastLine);

  const Rope &rope = text.rope();
  // Lines the tree covers: all of them, or those wholly inside a window.
  qsizetype coverFirst = 0, coverLast = rope.lineCount() - 1;
  if (m_treeWindowed) {
    coverFirst = m_winStart > 0 ? rope.lineAt(m_winStart) + 1 : 0;
    coverLast = m_winEnd >= m_doc->length() ? rope.lineCount() - 1 : rope.lineAt(m_winEnd) - 1;
  }
  const qsizetype treeFirst = qMax(firstLine, coverFirst), treeLast = qMin(lastLine, coverLast);

  QList<FoldRange> result;
  if (firstLine < treeFirst)
    result += fallback.foldRanges(text, firstLine, qMin(lastLine, treeFirst - 1));
  if (treeFirst <= treeLast) {
    std::map<qsizetype, qsizetype> found;
    TSQueryCursor *cursor = ts_query_cursor_new();
    collectFolds(cursor, ts_tree_root_node(m_tree.get()), *m_lang, rope, treeFirst, treeLast, found);
    for (const InjectedLayer &layer : m_layers) {
      if (!layer.language || !layer.language->folds || !layer.tree)
        continue;
      const qsizetype a = qMax(treeFirst, rope.lineAt(layer.start)), b = qMin(treeLast, rope.lineAt(layer.end));
      if (a <= b)
        collectFolds(cursor, ts_tree_root_node(layer.tree.get()), *layer.language, rope, a, b, found);
    }
    ts_query_cursor_delete(cursor);
    for (const auto &[header, end] : found)
      result.append({header, end});
  }
  if (treeLast < lastLine)
    result += fallback.foldRanges(text, qMax(firstLine, treeLast + 1), lastLine);
  std::sort(result.begin(), result.end(), [](const FoldRange &a, const FoldRange &b) { return a.startLine < b.startLine; });
  return result;
}

} // namespace qce
