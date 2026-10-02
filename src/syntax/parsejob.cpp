#include "syntax/parsejob.h"

#include <QtCore/QElapsedTimer>

#include <map>

namespace qce {

namespace {

bool progress(TSParseState *state) {
  return static_cast<const std::atomic_bool *>(state->payload)->load(std::memory_order_relaxed);
}

TSRange rangeOf(const Rope &rope, qsizetype start, qsizetype end) {
  return TSRange{toPoint(rope.positionAt(start)), toPoint(rope.positionAt(end)), toByte(start), toByte(end)};
}

TreePtr parseWithRanges(
  TSParser *parser, const TSLanguage *language, const Rope &rope, const TSTree *oldTree,
  const std::vector<TSRange> &ranges, std::atomic_bool *cancel
) {
  ts_parser_set_language(parser, language);
  if (ranges.empty() || !ts_parser_set_included_ranges(parser, ranges.data(), uint32_t(ranges.size()))) {
    ts_parser_set_included_ranges(parser, nullptr, 0);
    if (!ranges.empty())
      return nullptr; // invalid ranges: nothing sensible to parse
  }
  RopeInput input(rope);
  TSParseOptions options{cancel, &progress};
  TreePtr tree(ts_parser_parse_with_options(parser, oldTree, input.input(), options));
  if (!tree)
    ts_parser_reset(parser); // a cancelled parse leaves resumable state we don't want
  return tree;
}

struct InjectionRun {
  const Rope &rope;
  std::atomic_bool *cancel;
  std::vector<InjectedLayer> &out;
  std::map<QString, ParserPtr> parsers;

  TSParser *parserFor(const QString &id) {
    ParserPtr &p = parsers[id];
    if (!p)
      p.reset(ts_parser_new());
    return p.get();
  }

  void run(const TSTree *tree, const CompiledLanguage &host, qsizetype from, qsizetype to, int depth) {
    if (!host.injections || depth >= MaxInjectionDepth || cancel->load(std::memory_order_relaxed))
      return;
    const QueryInfo &info = host.injectionInfo;
    TSQueryCursor *cursor = ts_query_cursor_new();
    ts_query_cursor_set_byte_range(cursor, toByte(from), toByte(to));
    ts_query_cursor_exec(cursor, host.injections, ts_tree_root_node(tree));

    struct Pending {
      std::shared_ptr<const CompiledLanguage> language;
      std::vector<TSRange> ranges;
    };
    std::map<QString, Pending> combined;
    TSQueryMatch match;
    while (ts_query_cursor_next_match(cursor, &match)) {
      if (!predicatesHold(info, match, rope))
        continue;
      const PatternInfo &pattern = info.patterns[match.pattern_index];
      QString languageName = pattern.injectionLanguage;
      TSNode content{};
      bool haveContent = false;
      for (uint16_t i = 0; i < match.capture_count; ++i) {
        const TSQueryCapture &c = match.captures[i];
        if (int(c.index) == info.injectionContent) {
          content = c.node;
          haveContent = true;
        } else if (int(c.index) == info.injectionLanguage && languageName.isEmpty()) {
          languageName = nodeText(rope, c.node, 64);
        }
      }
      if (!haveContent || languageName.isEmpty())
        continue;
      const LanguageInfo *target = LanguageRegistry::instance().find(languageName);
      if (!target)
        continue;
      auto compiled = LanguageRegistry::instance().compiled(target->id);
      if (!compiled || !compiled->highlights)
        continue;
      const qsizetype start = toUnit(ts_node_start_byte(content));
      const qsizetype end = toUnit(ts_node_end_byte(content));
      if (end <= start)
        continue;
      const TSRange range = rangeOf(rope, start, end);
      if (pattern.combined) {
        Pending &p = combined[target->id];
        p.language = compiled;
        p.ranges.push_back(range);
      } else {
        parseLayer(compiled, {range}, from, to, depth);
      }
    }
    ts_query_cursor_delete(cursor);
    for (auto &[id, p] : combined)
      parseLayer(p.language, std::move(p.ranges), from, to, depth);
  }

  void parseLayer(
    const std::shared_ptr<const CompiledLanguage> &language, std::vector<TSRange> ranges, qsizetype from,
    qsizetype to, int depth
  ) {
    if (cancel->load(std::memory_order_relaxed))
      return;
    // Included ranges must be sorted and must not overlap.
    std::sort(ranges.begin(), ranges.end(), [](const TSRange &a, const TSRange &b) { return a.start_byte < b.start_byte; });
    TreePtr tree = parseWithRanges(parserFor(language->info->id), language->language, rope, nullptr, ranges, cancel);
    if (!tree)
      return;
    InjectedLayer layer;
    layer.language = language;
    layer.start = toUnit(ranges.front().start_byte);
    layer.end = toUnit(ranges.back().end_byte);
    const TSTree *raw = tree.get();
    out.push_back(std::move(layer));
    out.back().tree = std::move(tree);
    run(raw, *language, from, to, depth + 1);
  }
};

} // namespace

ParseResult runParse(ParseRequest request) {
  ParseResult result;
  result.generation = request.generation;
  result.version = request.snapshot.version();
  result.windowed = request.windowed;
  const Rope &rope = request.snapshot.rope();
  std::atomic_bool *cancel = request.cancel.get();

  QElapsedTimer timer;
  timer.start();
  ParserPtr parser(ts_parser_new());
  std::vector<TSRange> ranges;
  if (request.windowed) {
    result.start = qBound<qsizetype>(0, request.start, rope.length());
    result.end = qBound(result.start, request.end, rope.length());
    ranges.push_back(rangeOf(rope, result.start, result.end));
  } else {
    result.start = 0;
    result.end = rope.length();
  }
  result.tree =
    parseWithRanges(parser.get(), request.language->language, rope, request.oldTree.get(), ranges, cancel);
  result.parseNs = timer.nsecsElapsed();
  if (!result.tree) {
    result.cancelled = true;
    return result;
  }

  if (request.language->injections) {
    timer.restart();
    result.injectionStart = qBound<qsizetype>(result.start, request.injectionStart, result.end);
    result.injectionEnd = qBound(result.injectionStart, request.injectionEnd, result.end);
    InjectionRun run{rope, cancel, result.layers, {}};
    run.run(result.tree.get(), *request.language, result.injectionStart, result.injectionEnd, 0);
    result.injectionNs = timer.nsecsElapsed();
    if (cancel->load(std::memory_order_relaxed)) {
      result.cancelled = true;
      result.tree.reset();
      result.layers.clear();
    }
  }
  return result;
}

} // namespace qce
