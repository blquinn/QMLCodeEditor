#include "syntax/queryinfo.h"

#include <QtCore/QHash>

using namespace Qt::StringLiterals;

namespace qce {

std::optional<TokenStyle> styleForCapture(QStringView name) {
  struct Entry {
    QLatin1StringView prefix;
    std::optional<TokenStyle> style;
  };
  // Longest matching dotted prefix wins, so more specific names come out of the same table.
  static const QHash<QString, std::optional<TokenStyle>> table = [] {
    const Entry entries[] = {
      {"keyword"_L1, TokenStyle::Keyword},
      {"keyword.directive"_L1, TokenStyle::Preprocessor},
      {"preproc"_L1, TokenStyle::Preprocessor},
      {"include"_L1, TokenStyle::Preprocessor},
      {"operator"_L1, TokenStyle::Operator},
      {"string"_L1, TokenStyle::String},
      {"string.special.key"_L1, TokenStyle::Property},
      {"string.escape"_L1, TokenStyle::Constant},
      {"escape"_L1, TokenStyle::Constant},
      {"character"_L1, TokenStyle::String},
      {"comment"_L1, TokenStyle::Comment},
      {"number"_L1, TokenStyle::Number},
      {"float"_L1, TokenStyle::Number},
      {"boolean"_L1, TokenStyle::Constant},
      {"type"_L1, TokenStyle::Type},
      {"constructor"_L1, TokenStyle::Type},
      {"module"_L1, TokenStyle::Type},
      {"function"_L1, TokenStyle::Function},
      {"method"_L1, TokenStyle::Function},
      {"function.macro"_L1, TokenStyle::Preprocessor},
      {"variable"_L1, TokenStyle::Variable},
      {"variable.builtin"_L1, TokenStyle::Constant},
      {"parameter"_L1, TokenStyle::Variable},
      {"constant"_L1, TokenStyle::Constant},
      {"label"_L1, TokenStyle::Constant},
      {"property"_L1, TokenStyle::Property},
      {"field"_L1, TokenStyle::Property},
      {"attribute"_L1, TokenStyle::Attribute},
      {"tag"_L1, TokenStyle::Tag},
      {"punctuation"_L1, TokenStyle::Punctuation},
      {"delimiter"_L1, TokenStyle::Punctuation},
      {"text.title"_L1, TokenStyle::Heading},
      {"markup.heading"_L1, TokenStyle::Heading},
      {"text.strong"_L1, TokenStyle::Strong},
      {"markup.bold"_L1, TokenStyle::Strong},
      {"markup.strong"_L1, TokenStyle::Strong},
      {"text.emphasis"_L1, TokenStyle::Emphasis},
      {"markup.italic"_L1, TokenStyle::Emphasis},
      {"text.uri"_L1, TokenStyle::Link},
      {"text.reference"_L1, TokenStyle::Link},
      {"markup.link"_L1, TokenStyle::Link},
      {"text.literal"_L1, TokenStyle::Code},
      {"markup.raw"_L1, TokenStyle::Code},
      {"none"_L1, TokenStyle::Default},
    };
    QHash<QString, std::optional<TokenStyle>> map;
    for (const Entry &e : entries)
      map.insert(QString(e.prefix), e.style);
    return map;
  }();
  QStringView prefix = name;
  while (!prefix.isEmpty()) {
    // Host-registered styles (API-13) win over the built-in table at the same prefix.
    if (const auto custom = tokenStyleFromName(prefix); custom && isCustomTokenStyle(*custom))
      return custom;
    if (auto it = table.constFind(prefix.toString()); it != table.constEnd())
      return it.value();
    const qsizetype dot = prefix.lastIndexOf(u'.');
    if (dot < 0)
      break;
    prefix = prefix.left(dot);
  }
  return std::nullopt;
}

QueryInfo QueryInfo::analyze(const TSQuery *query) {
  QueryInfo info;
  const uint32_t captures = ts_query_capture_count(query);
  info.captureStyles.resize(captures);
  info.captureDepth.resize(captures);
  for (uint32_t i = 0; i < captures; ++i) {
    uint32_t length = 0;
    const char *name = ts_query_capture_name_for_id(query, i, &length);
    const QString captureName = QString::fromUtf8(name, qsizetype(length));
    if (captureName == u"injection.content"_s)
      info.injectionContent = int(i);
    else if (captureName == u"injection.language"_s)
      info.injectionLanguage = int(i);
    info.captureStyles[i] = styleForCapture(captureName);
    info.captureDepth[i] = uint8_t(qMin<qsizetype>(captureName.count(u'.') + 1, 255));
  }

  const uint32_t patterns = ts_query_pattern_count(query);
  info.patterns.resize(patterns);
  for (uint32_t p = 0; p < patterns; ++p) {
    PatternInfo &pattern = info.patterns[p];
    uint32_t stepCount = 0;
    const TSQueryPredicateStep *steps = ts_query_predicates_for_pattern(query, p, &stepCount);
    auto stringValue = [&](uint32_t id) {
      uint32_t length = 0;
      const char *s = ts_query_string_value_for_id(query, id, &length);
      return QString::fromUtf8(s, qsizetype(length));
    };
    uint32_t i = 0;
    while (i < stepCount) {
      uint32_t end = i;
      while (end < stepCount && steps[end].type != TSQueryPredicateStepTypeDone)
        ++end;
      if (end > i && steps[i].type == TSQueryPredicateStepTypeString) {
        const QString name = stringValue(steps[i].value_id);
        const uint32_t argc = end - i - 1;
        const TSQueryPredicateStep *args = steps + i + 1;
        auto isCapture = [&](uint32_t k) { return k < argc && args[k].type == TSQueryPredicateStepTypeCapture; };
        auto isString = [&](uint32_t k) { return k < argc && args[k].type == TSQueryPredicateStepTypeString; };
        if (name == u"set!"_s) {
          if (argc >= 2 && isString(0) && isString(1)) {
            const QString key = stringValue(args[0].value_id);
            if (key == u"injection.language"_s)
              pattern.injectionLanguage = stringValue(args[1].value_id);
          } else if (argc >= 1 && isString(0) && stringValue(args[0].value_id) == u"injection.combined"_s) {
            pattern.combined = true;
          }
        } else if ((name == u"eq?"_s || name == u"not-eq?"_s) && isCapture(0) && argc >= 2) {
          QueryPredicate pr;
          pr.kind = name == u"eq?"_s ? QueryPredicate::Kind::Eq : QueryPredicate::Kind::NotEq;
          pr.capture = args[0].value_id;
          if (isCapture(1)) {
            pr.rhsIsCapture = true;
            pr.rhsCapture = args[1].value_id;
          } else if (isString(1)) {
            pr.literal = stringValue(args[1].value_id);
          } else {
            i = end + 1;
            continue;
          }
          pattern.predicates.push_back(std::move(pr));
        } else if (
          (name == u"match?"_s || name == u"not-match?"_s || name == u"lua-match?"_s || name == u"not-lua-match?"_s ||
           name == u"vim-match?"_s) &&
          isCapture(0) && isString(1)
        ) {
          QueryPredicate pr;
          pr.kind = (name == u"match?"_s || name == u"lua-match?"_s || name == u"vim-match?"_s)
                      ? QueryPredicate::Kind::Match
                      : QueryPredicate::Kind::NotMatch;
          pr.capture = args[0].value_id;
          pr.regex = QRegularExpression(stringValue(args[1].value_id));
          if (pr.regex.isValid())
            pattern.predicates.push_back(std::move(pr));
        } else if ((name == u"any-of?"_s || name == u"not-any-of?"_s) && isCapture(0)) {
          QueryPredicate pr;
          pr.kind = name == u"any-of?"_s ? QueryPredicate::Kind::AnyOf : QueryPredicate::Kind::NotAnyOf;
          pr.capture = args[0].value_id;
          for (uint32_t k = 1; k < argc; ++k)
            if (isString(k))
              pr.literals.append(stringValue(args[k].value_id));
          pattern.predicates.push_back(std::move(pr));
        }
        // #is-not?, #offset!, #strip!, #select-adjacent! ...: not needed for styling.
      }
      i = end + 1;
    }
  }
  return info;
}

QString nodeText(const Rope &rope, TSNode node, qsizetype maxUnits) {
  const qsizetype start = toUnit(ts_node_start_byte(node));
  const qsizetype end = toUnit(ts_node_end_byte(node));
  return rope.toString(start, qMin(end, start + maxUnits));
}

namespace {
bool findCapture(const TSQueryMatch &match, uint32_t id, TSNode *out) {
  for (uint16_t i = 0; i < match.capture_count; ++i) {
    if (match.captures[i].index == id) {
      *out = match.captures[i].node;
      return true;
    }
  }
  return false;
}
} // namespace

bool predicatesHold(const QueryInfo &info, const TSQueryMatch &match, const Rope &rope) {
  if (match.pattern_index >= info.patterns.size())
    return true;
  for (const QueryPredicate &pr : info.patterns[match.pattern_index].predicates) {
    TSNode node;
    if (!findCapture(match, pr.capture, &node))
      continue; // optional capture not present: nothing to test
    switch (pr.kind) {
    case QueryPredicate::Kind::Eq:
    case QueryPredicate::Kind::NotEq: {
      bool equal;
      if (pr.rhsIsCapture) {
        TSNode other;
        if (!findCapture(match, pr.rhsCapture, &other))
          continue;
        const uint32_t la = ts_node_end_byte(node) - ts_node_start_byte(node);
        const uint32_t lb = ts_node_end_byte(other) - ts_node_start_byte(other);
        equal = la == lb && nodeText(rope, node) == nodeText(rope, other);
      } else {
        // Compare lengths first: a long string literal never equals a short keyword.
        const uint32_t units = (ts_node_end_byte(node) - ts_node_start_byte(node)) / 2;
        equal = qsizetype(units) == pr.literal.size() && nodeText(rope, node, pr.literal.size()) == pr.literal;
      }
      if (equal != (pr.kind == QueryPredicate::Kind::Eq))
        return false;
      break;
    }
    case QueryPredicate::Kind::Match:
    case QueryPredicate::Kind::NotMatch: {
      const bool matches = pr.regex.match(nodeText(rope, node, 512)).hasMatch();
      if (matches != (pr.kind == QueryPredicate::Kind::Match))
        return false;
      break;
    }
    case QueryPredicate::Kind::AnyOf:
    case QueryPredicate::Kind::NotAnyOf: {
      const uint32_t units = (ts_node_end_byte(node) - ts_node_start_byte(node)) / 2;
      bool found = false;
      if (units <= 128) {
        const QString text = nodeText(rope, node, 128);
        found = pr.literals.contains(text);
      }
      if (found != (pr.kind == QueryPredicate::Kind::AnyOf))
        return false;
      break;
    }
    }
  }
  return true;
}

} // namespace qce
