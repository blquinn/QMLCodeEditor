// Search and the ex command line of the vim handler.
#include "core/vim/vimhandler.h"

#include "core/textboundaries.h"
#include "core/textsearch.h"
#include "core/vim/vimkeys.h"
#include "core/vim/vimmotions.h"

#include <optional>

using namespace qce::vim;
using namespace Qt::StringLiterals;

namespace qce {

namespace {

// Splits "/a/b/flags" at unescaped delimiters; "\/" becomes "/".
QStringList splitDelimited(const QString &text, QChar delimiter, int maxParts) {
  QStringList parts;
  QString current;
  for (qsizetype i = 0; i < text.size(); ++i) {
    const QChar c = text[i];
    if (c == u'\\' && i + 1 < text.size()) {
      if (text[i + 1] == delimiter) {
        current += delimiter;
        ++i;
        continue;
      }
      current += c;
      current += text[++i];
      continue;
    }
    if (c == delimiter && parts.size() < maxParts - 1) {
      parts.append(current);
      current.clear();
      continue;
    }
    current += c;
  }
  parts.append(current);
  return parts;
}

} // namespace

bool VimInputHandler::compileSearch(const QString &pattern, vim::CompiledPattern *out) {
  *out = vim::compilePattern(pattern, m_options);
  if (!out->valid()) {
    setMessage(out->error);
    return false;
  }
  return true;
}

void VimInputHandler::highlightSearch() {
  if (!m_host)
    return;
  if (m_hlsearch && !m_noHighlight && m_search.compiled.valid())
    m_host->setSearchHighlight(m_search.compiled.regex);
  else
    m_host->setSearchHighlight(QRegularExpression());
}

VimInputHandler::Target VimInputHandler::evalSearch(
  const QStringList &keys, int count, qsizetype off, bool flip, bool backwardKey, bool useLast
) {
  Target t;
  t.from = off;
  t.to = off;
  const QString &k = keys.constFirst();
  const Rope &r = rope();
  QString pattern;
  bool forward = true;
  if (k == u"/"_s || k == u"?"_s) {
    for (int i = 1; i < keys.size() - 1; ++i)
      pattern += keys[i];
    forward = k == u"/"_s;
    if (pattern.isEmpty()) {
      if (m_search.pattern.isEmpty()) {
        setMessage(u"E35: No previous regular expression"_s);
        return t;
      }
      pattern = m_search.pattern;
    }
    if (m_history[1].isEmpty() || m_history[1].last() != formatKeys(parseKeys(pattern)))
      m_history[1].append(pattern);
    if (pattern != m_search.pattern)
      m_search.compiled = {};
    m_search.pattern = pattern;
    m_search.forward = forward;
  } else if (useLast) {
    if (m_search.pattern.isEmpty()) {
      setMessage(u"E35: No previous regular expression"_s);
      return t;
    }
    pattern = m_search.pattern;
    forward = flip ? !m_search.forward : m_search.forward;
  } else {
    // * and #: the keyword under or after the cursor, else the non-blank run.
    const qsizetype line = r.lineAt(off);
    const qsizetype start = r.lineStart(line), end = r.lineEnd(line);
    qsizetype a = off;
    while (a < end && classAt(r, a) != CharClass::Word)
      a = TextBoundaries(r).nextGrapheme(a);
    bool keyword = a < end;
    if (!keyword) {
      a = off;
      while (a < end && classAt(r, a) == CharClass::Blank)
        ++a;
      if (a >= end) {
        setMessage(u"E348: No string under cursor"_s);
        return t;
      }
    }
    const CharClass cls = keyword ? CharClass::Word : classAt(r, a);
    qsizetype b = a;
    while (a > start && classAt(r, a - 1) == cls)
      --a;
    while (b < end && classAt(r, b) == cls)
      ++b;
    const QString word = r.toString(a, b);
    pattern = keyword ? u"\\<"_s + word + u"\\>"_s : u"\\V"_s + QString(word).replace(u"\\"_s, u"\\\\"_s);
    forward = !backwardKey;
    if (pattern != m_search.pattern)
      m_search.compiled = {};
    m_search.pattern = pattern;
    m_search.forward = forward;
  }
  m_noHighlight = false;
  if (!m_search.compiled.valid() && !compileSearch(pattern, &m_search.compiled))
    return t;
  qsizetype pos = off;
  for (int i = 0; i < qMax(1, count); ++i) {
    const std::optional<Selection> m = search::find(r, m_search.compiled, pos, forward, true);
    if (!m) {
      setMessage(u"E486: Pattern not found: "_s + pattern);
      return t;
    }
    pos = m->anchor;
  }
  highlightSearch();
  t.ok = true;
  t.to = pos;
  t.kind = Kind::Exclusive;
  t.jump = true;
  t.bigMotion = true;
  return t;
}

// ---------------------------------------------------------------------------------------------
// Ex

bool VimInputHandler::parseExRange(const QString &t, qsizetype *pos, ExRange *range) {
  const Rope &r = rope();
  const qsizetype lastLine = r.lineCount() - 1;
  const qsizetype current = r.lineAt(m_sel->primary().head);
  range->first = range->last = current;
  range->given = false;
  auto skipSpaces = [&] {
    while (*pos < t.size() && t[*pos].isSpace())
      ++*pos;
  };
  skipSpaces();
  if (*pos < t.size() && t[*pos] == u'%') {
    range->first = 0;
    range->last = lastLine;
    range->given = true;
    ++*pos;
    return true;
  }
  bool error = false;
  auto parseAddress = [&](qsizetype from) -> std::optional<qsizetype> {
    skipSpaces();
    if (*pos >= t.size())
      return std::nullopt;
    qsizetype base = from;
    bool have = false;
    const QChar c = t[*pos];
    if (c.isDigit()) {
      qsizetype value = 0;
      while (*pos < t.size() && t[*pos].isDigit())
        value = value * 10 + t[(*pos)++].digitValue();
      base = qMax<qsizetype>(0, value - 1);
      have = true;
    } else if (c == u'.') {
      ++*pos;
      have = true;
    } else if (c == u'$') {
      ++*pos;
      base = lastLine;
      have = true;
    } else if (c == u'\'') {
      if (*pos + 1 >= t.size()) {
        error = true;
        return std::nullopt;
      }
      const qsizetype at = markOffset(t[*pos + 1]);
      *pos += 2;
      if (at < 0) {
        setMessage(u"E20: Mark not set"_s);
        error = true;
        return std::nullopt;
      }
      base = r.lineAt(at);
      have = true;
    } else if (c == u'/' || c == u'?') {
      const QStringList parts = splitDelimited(t.mid(*pos + 1), c, 2);
      const QString pattern = parts.value(0);
      *pos += 1 + pattern.size() + 1;
      if (parts.size() > 1)
        ; // the closing delimiter was consumed above
      else
        *pos = t.size();
      vim::CompiledPattern compiled;
      if (!compileSearch(pattern.isEmpty() ? m_search.pattern : pattern, &compiled)) {
        error = true;
        return std::nullopt;
      }
      const qsizetype startOffset = c == u'/' ? r.lineEnd(from) : r.lineStart(from);
      const std::optional<Selection> m = search::find(r, compiled, startOffset, c == u'/', true);
      if (!m) {
        setMessage(u"E486: Pattern not found: "_s + pattern);
        error = true;
        return std::nullopt;
      }
      base = r.lineAt(m->anchor);
      have = true;
    } else if (c == u'+' || c == u'-') {
      have = true;
    }
    if (!have)
      return std::nullopt;
    for (;;) {
      skipSpaces();
      if (*pos >= t.size() || (t[*pos] != u'+' && t[*pos] != u'-'))
        break;
      const int sign = t[*pos] == u'+' ? 1 : -1;
      ++*pos;
      qsizetype value = 0;
      bool digits = false;
      while (*pos < t.size() && t[*pos].isDigit()) {
        value = value * 10 + t[(*pos)++].digitValue();
        digits = true;
      }
      base += sign * (digits ? value : 1);
    }
    return base;
  };

  const std::optional<qsizetype> first = parseAddress(current);
  if (error)
    return false;
  if (!first)
    return true;
  qsizetype a = *first, b = *first;
  skipSpaces();
  if (*pos < t.size() && (t[*pos] == u',' || t[*pos] == u';')) {
    const bool semicolon = t[*pos] == u';';
    ++*pos;
    const std::optional<qsizetype> second =
      parseAddress(semicolon ? qBound<qsizetype>(0, a, lastLine) : current);
    if (error)
      return false;
    b = second ? *second : current;
  }
  a = qBound<qsizetype>(0, a, lastLine);
  b = qBound<qsizetype>(0, b, lastLine);
  if (a > b)
    std::swap(a, b);
  range->first = a;
  range->last = b;
  range->given = true;
  return true;
}

void VimInputHandler::executeEx(const QString &line, bool nested) {
  QString text = line.trimmed();
  while (text.startsWith(u':'))
    text.remove(0, 1);
  text = text.trimmed();
  if (text.isEmpty())
    return;
  if (!nested) {
    if (m_history[0].isEmpty() || m_history[0].last() != text)
      m_history[0].append(text);
    m_lastEx = text;
  }
  const Rope &r = rope();
  qsizetype pos = 0;
  ExRange range;
  if (!parseExRange(text, &pos, &range)) {
    m_failed = true;
    return;
  }
  while (pos < text.size() && text[pos].isSpace())
    ++pos;
  QString name;
  while (pos < text.size() && text[pos].isLetter())
    name += text[pos++];
  int shifts = 0;
  if (name.isEmpty() && pos < text.size() && (text[pos] == u'>' || text[pos] == u'<')) {
    name = QString(text[pos]);
    while (pos < text.size() && text[pos] == name[0]) {
      ++shifts;
      ++pos;
    }
  }
  bool bang = false;
  if (pos < text.size() && text[pos] == u'!' && !name.isEmpty()) {
    bang = true;
    ++pos;
  }
  const QString rest = text.mid(pos);
  const QString args = rest.trimmed();
  auto lineRange = [&](const ExRange &e) {
    Range rg;
    rg.start = r.lineStart(e.first);
    rg.end = e.last + 1 < r.lineCount() ? r.lineStart(e.last + 1) : r.length();
    rg.linewise = true;
    rg.cursor = rg.start;
    return rg;
  };
  auto is = [&](const char *full, int minimum) {
    return name.size() >= minimum && QLatin1StringView(full).startsWith(name);
  };

  if (name.isEmpty()) {
    if (range.given) {
      pushJump(m_sel->primary().head);
      setCursors({firstNonBlank(r, range.last)}, 0);
    }
    return;
  }
  if (
    name == u"wq"_s || name == u"wqa"_s || name == u"wqall"_s || is("xit", 1) || name == u"xa"_s ||
    name == u"xall"_s
  ) {
    emit writeRequested(args);
    emit quitRequested(bang);
  } else if (is("write", 1) || name == u"wa"_s || name == u"wall"_s) {
    emit writeRequested(args);
  } else if (is("quit", 1) || name == u"qa"_s || name == u"qall"_s || name == u"cq"_s) {
    emit quitRequested(bang);
  } else if (is("set", 2)) {
    exSet(args);
  } else if (is("substitute", 1) || (name == u"s"_s)) {
    exSubstitute(range, rest, false);
  } else if (is("global", 1)) {
    exGlobal(range.given ? range : ExRange{0, r.lineCount() - 1, true}, rest, bang);
  } else if (is("vglobal", 1)) {
    exGlobal(range.given ? range : ExRange{0, r.lineCount() - 1, true}, rest, true);
  } else if (is("delete", 1)) {
    exDelete(range, args, false);
  } else if (is("yank", 1)) {
    exDelete(range, args, true);
  } else if (is("normal", 4)) {
    exNormal(range, rest.startsWith(u' ') ? rest.mid(1) : rest);
  } else if (is("nohlsearch", 3)) {
    m_noHighlight = true;
    highlightSearch();
  } else if (name == u">"_s || name == u"<"_s) {
    if (m_ctx->settings.readOnly)
      return;
    shiftLines({lineRange(range)}, name == u">"_s, qMax(1, shifts));
  } else if (is("mark", 2) || name == u"k"_s) {
    if (!args.isEmpty())
      setMark(args[0], r.lineStart(range.last));
  } else {
    emit exCommand(text);
  }
}

void VimInputHandler::exSet(const QString &args) {
  for (const QString &option : args.split(u' ', Qt::SkipEmptyParts)) {
    if (option == u"ic"_s || option == u"ignorecase"_s)
      setIgnoreCase(true);
    else if (option == u"noic"_s || option == u"noignorecase"_s)
      setIgnoreCase(false);
    else if (option == u"scs"_s || option == u"smartcase"_s)
      setSmartCase(true);
    else if (option == u"noscs"_s || option == u"nosmartcase"_s)
      setSmartCase(false);
    else if (option == u"hls"_s || option == u"hlsearch"_s) {
      m_hlsearch = true;
      highlightSearch();
    } else if (option == u"nohls"_s || option == u"nohlsearch"_s) {
      m_hlsearch = false;
      highlightSearch();
    } else {
      setMessage(u"E518: Unknown option: "_s + option);
    }
  }
}

void VimInputHandler::exSubstitute(const ExRange &range, const QString &rest, bool) {
  if (rest.size() < 2) {
    setMessage(u"E35: No previous regular expression"_s);
    return;
  }
  if (m_ctx->settings.readOnly || m_doc->isLoading())
    return;
  const QChar delimiter = rest[0];
  if (delimiter.isLetterOrNumber() || delimiter == u'\\' || delimiter == u'"' || delimiter == u'|') {
    setMessage(u"E146: Regular expressions can't be delimited by letters"_s);
    return;
  }
  const QStringList parts = splitDelimited(rest.mid(1), delimiter, 3);
  QString pattern = parts.value(0);
  const QString replacement = parts.value(1);
  const QString flags = parts.value(2).trimmed();
  if (pattern.isEmpty())
    pattern = m_search.pattern;
  if (pattern.isEmpty()) {
    setMessage(u"E35: No previous regular expression"_s);
    return;
  }
  vim::PatternOptions options = m_options;
  if (flags.contains(u'i')) {
    options.ignoreCase = true;
    options.smartCase = false;
  }
  if (flags.contains(u'I'))
    options.ignoreCase = false;
  const vim::CompiledPattern compiled = vim::compilePattern(pattern, options);
  if (!compiled.valid()) {
    setMessage(compiled.error);
    return;
  }
  const bool global = flags.contains(u'g');
  const bool countOnly = flags.contains(u'n');
  if (!flags.contains(u'i') && !flags.contains(u'I')) {
    if (pattern != m_search.pattern)
      m_search.pattern = pattern;
    m_search.compiled = compiled;
    m_noHighlight = false;
    highlightSearch();
  }
  const Rope &r = rope();
  QList<Edit> edits;
  qsizetype changedLines = 0, lastBase = -1;
  search::forEachLineMatch(
    r, compiled.regex, range.first, range.last,
    [&](qsizetype base, const QRegularExpressionMatch &m) {
      edits.append({base + m.capturedStart(), base + m.capturedEnd(), vim::expandReplacement(replacement, m)});
      if (base != lastBase) {
        ++changedLines;
        lastBase = base;
      }
      return true;
    },
    global
  );
  if (edits.isEmpty()) {
    setMessage(u"E486: Pattern not found: "_s + pattern);
    m_failed = true;
    return;
  }
  if (countOnly) {
    setMessage(u"%1 matches on %2 lines"_s.arg(edits.size()).arg(changedLines));
    return;
  }
  const EditResult res = edit(edits);
  if (!res.ok)
    return;
  const Rope &now = rope();
  const qsizetype line = now.lineAt(qMin(res.ends.last(), now.length()));
  setCursors({firstNonBlank(now, line)}, 0);
  if (changedLines > 2)
    setMessage(u"%1 substitutions on %2 lines"_s.arg(edits.size()).arg(changedLines));
}

void VimInputHandler::exGlobal(const ExRange &range, const QString &rest, bool invert) {
  if (rest.size() < 2) {
    setMessage(u"E35: No previous regular expression"_s);
    return;
  }
  const QChar delimiter = rest[0];
  const QStringList parts = splitDelimited(rest.mid(1), delimiter, 2);
  QString pattern = parts.value(0);
  const QString command = parts.value(1).trimmed();
  if (pattern.isEmpty())
    pattern = m_search.pattern;
  vim::CompiledPattern compiled;
  if (!compileSearch(pattern, &compiled))
    return;
  m_search.pattern = pattern;
  m_search.compiled = compiled;
  m_noHighlight = false;
  highlightSearch();
  const Rope &r = rope();
  AnchorSet &anchors = m_doc->anchors();
  QList<AnchorId> marked;
  for (qsizetype line = range.first; line <= range.last; ++line) {
    const QString text = r.toString(r.lineStart(line), r.lineEnd(line));
    if (compiled.regex.match(text).hasMatch() != invert)
      marked.append(anchors.create(r.lineStart(line), Gravity::Left));
  }
  if (marked.isEmpty()) {
    setMessage(u"E486: Pattern not found: "_s + pattern);
    return;
  }
  ++m_groupHold;
  ++m_depth;
  const QString savedEx = m_lastEx;
  for (const AnchorId id : marked) {
    if (!anchors.contains(id))
      continue;
    const Rope &now = rope();
    const qsizetype line = now.lineAt(anchors.offset(id));
    const QString text = now.toString(now.lineStart(line), now.lineEnd(line));
    if (compiled.regex.match(text).hasMatch() == invert)
      continue; // an earlier command changed this line so that it no longer qualifies
    setCursors({now.lineStart(line)}, 0);
    executeEx(command.isEmpty() ? u"p"_s : command, true);
    if (m_failed)
      break;
  }
  --m_depth;
  --m_groupHold;
  m_lastEx = savedEx;
  for (const AnchorId id : marked)
    if (anchors.contains(id))
      anchors.remove(id);
}

void VimInputHandler::exDelete(const ExRange &rangeIn, const QString &argsIn, bool yank) {
  ExRange range = rangeIn;
  QString args = argsIn;
  QString reg;
  if (!args.isEmpty() && !args[0].isDigit()) {
    reg = QString(args[0]);
    args = args.mid(1).trimmed();
  }
  if (!args.isEmpty() && args[0].isDigit()) {
    const int n = args.toInt();
    range.first = range.last;
    range.last = qMin<qsizetype>(range.first + qMax(1, n) - 1, rope().lineCount() - 1);
  }
  const Rope &r = rope();
  Range rg;
  rg.start = r.lineStart(range.first);
  rg.end = range.last + 1 < r.lineCount() ? r.lineStart(range.last + 1) : r.length();
  rg.linewise = true;
  rg.cursor = r.lineStart(range.first);
  Cmd cmd;
  cmd.reg = reg;
  applyOperator(yank ? u"y"_s : u"d"_s, {rg}, cmd, false);
}

void VimInputHandler::exNormal(const ExRange &range, const QString &keys) {
  const QStringList symbols = parseKeys(keys);
  AnchorSet &anchors = m_doc->anchors();
  QList<AnchorId> lines;
  if (range.given) {
    const Rope &r = rope();
    for (qsizetype line = range.first; line <= range.last; ++line)
      lines.append(anchors.create(r.lineStart(line), Gravity::Left));
  }
  auto run = [&] {
    m_cmdKeys.clear();
    for (const QString &symbol : symbols)
      process(symbol);
    // An unfinished command or insert ends like <Esc> (vim: "an incomplete command is aborted").
    if (m_mode == Mode::Insert || m_mode == Mode::Replace)
      finishInsert();
    if (m_mode == Mode::Visual || m_mode == Mode::VisualLine || m_mode == Mode::VisualBlock)
      exitVisual();
    clearPending();
    m_cmdKeys.clear();
    m_cmdChange = false;
  };
  ++m_groupHold;
  ++m_depth;
  if (lines.isEmpty()) {
    run();
  } else {
    for (const AnchorId id : lines) {
      if (!anchors.contains(id))
        continue;
      setCursors({anchors.offset(id)}, 0);
      run();
      if (m_failed)
        break;
    }
  }
  --m_depth;
  --m_groupHold;
  for (const AnchorId id : lines)
    if (anchors.contains(id))
      anchors.remove(id);
}

} // namespace qce
