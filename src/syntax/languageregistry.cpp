#include "syntax/languageregistry.h"

#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QRegularExpression>

#include <map>

extern "C" {
const TSLanguage *tree_sitter_c();
const TSLanguage *tree_sitter_cpp();
const TSLanguage *tree_sitter_json();
const TSLanguage *tree_sitter_javascript();
const TSLanguage *tree_sitter_qmljs();
const TSLanguage *tree_sitter_python();
const TSLanguage *tree_sitter_html();
const TSLanguage *tree_sitter_xml();
const TSLanguage *tree_sitter_markdown();
const TSLanguage *tree_sitter_markdown_inline();
}

using namespace Qt::StringLiterals;

namespace qce {

CompiledLanguage::~CompiledLanguage() {
  if (highlights)
    ts_query_delete(highlights);
  if (injections)
    ts_query_delete(injections);
}

namespace {

QString readResource(const QString &path) {
  QFile file(u":/qce/syntax/"_s + path);
  if (!file.open(QIODevice::ReadOnly))
    return {};
  return QString::fromUtf8(file.readAll());
}

// Splits query source into its top-level forms (patterns, and the comments and blank lines between
// them stay attached to the following one). Parentheses and brackets inside strings and comments
// are ignored. Returns [start, end) UTF-16 ranges.
QList<std::pair<qsizetype, qsizetype>> splitPatterns(const QString &s) {
  QList<std::pair<qsizetype, qsizetype>> out;
  qsizetype depth = 0, start = 0, i = 0;
  const qsizetype n = s.size();
  while (i < n) {
    const QChar c = s[i];
    if (c == u';') {
      while (i < n && s[i] != u'\n')
        ++i;
      continue;
    }
    if (c == u'"') {
      for (++i; i < n && s[i] != u'"'; ++i)
        if (s[i] == u'\\')
          ++i;
      ++i;
      if (depth == 0) { // a bare string at top level is a pattern by itself (rare)
        out.append({start, qMin(i, n)});
        start = i;
      }
      continue;
    }
    if (c == u'(' || c == u'[')
      ++depth;
    else if (c == u')' || c == u']') {
      if (depth > 0 && --depth == 0) {
        // a pattern may be followed by captures/predicates outside its parens: `(a) @x`
        qsizetype j = i + 1;
        while (j < n) {
          while (j < n && s[j].isSpace())
            ++j;
          if (j < n && (s[j] == u'@' || s[j] == u'*' || s[j] == u'+' || s[j] == u'?')) {
            while (j < n && !s[j].isSpace() && s[j] != u'(' && s[j] != u'[')
              ++j;
          } else
            break;
        }
        out.append({start, j});
        start = i = j;
        continue;
      }
    }
    ++i;
  }
  if (start < n && !s.mid(start).trimmed().isEmpty())
    out.append({start, n});
  return out;
}

} // namespace

TSQuery *compileQuery(const TSLanguage *language, const QString &source, QStringList *warnings) {
  QList<std::pair<qsizetype, qsizetype>> patterns;
  QString text = source;
  // The usual case compiles first time; only on an error do we cut patterns out.
  for (int attempt = 0; attempt < 200; ++attempt) {
    const QByteArray utf8 = text.toUtf8();
    uint32_t errorOffset = 0;
    TSQueryError error = TSQueryErrorNone;
    if (TSQuery *query = ts_query_new(language, utf8.constData(), uint32_t(utf8.size()), &errorOffset, &error)) {
      if (ts_query_pattern_count(query) == 0) {
        ts_query_delete(query);
        return nullptr;
      }
      return query;
    }
    // Map the byte offset back to a UTF-16 position, then drop the top-level pattern holding it.
    const qsizetype pos = QString::fromUtf8(utf8.left(errorOffset)).size();
    patterns = splitPatterns(text);
    bool dropped = false;
    for (const auto &[from, to] : std::as_const(patterns)) {
      if (pos >= from && pos < to + 1 && !(pos == to && to < text.size() && !text[pos].isSpace())) {
        if (warnings)
          warnings->append(
            u"dropped query pattern (error %1): %2"_s.arg(int(error)).arg(text.mid(from, to - from).trimmed().left(80))
          );
        text.remove(from, to - from);
        dropped = true;
        break;
      }
    }
    if (!dropped)
      return nullptr; // error outside any pattern (unbalanced input)
  }
  return nullptr;
}

LanguageRegistry::LanguageRegistry() {
  auto add = [this](LanguageInfo info) { m_languages.append(std::move(info)); };
  add({u"c"_s, u"C"_s, {u"c"_s, u"h"_s}, {}, {}, {u"c"_s}, &tree_sitter_c, {u"c/highlights.scm"_s}, {}});
  add(
    {u"cpp"_s,
     u"C++"_s,
     {u"cpp"_s, u"cc"_s, u"cxx"_s, u"hpp"_s, u"hh"_s, u"hxx"_s, u"ipp"_s, u"inl"_s, u"tpp"_s},
     {},
     {},
     {u"c++"_s, u"cplusplus"_s},
     &tree_sitter_cpp,
     {u"c/highlights.scm"_s, u"cpp/highlights.scm"_s},
     {u"cpp/injections.scm"_s}}
  );
  add({u"json"_s,
       u"JSON"_s,
       {u"json"_s, u"jsonc"_s, u"geojson"_s, u"webmanifest"_s},
       {u".babelrc"_s, u".eslintrc"_s},
       {},
       {u"jsonc"_s},
       &tree_sitter_json,
       {u"json/highlights.scm"_s},
       {}});
  add({u"javascript"_s,
       u"JavaScript"_s,
       {u"js"_s, u"mjs"_s, u"cjs"_s, u"jsx"_s},
       {},
       {u"node"_s, u"nodejs"_s, u"deno"_s},
       {u"js"_s, u"node"_s},
       &tree_sitter_javascript,
       {u"javascript/highlights.scm"_s},
       {}});
  // The QML grammar parses JavaScript natively inside bindings and functions, so no injection is
  // needed for it; its queries build on the JavaScript and TypeScript ones.
  add({u"qml"_s,
       u"QML"_s,
       {u"qml"_s},
       {},
       {u"qml"_s, u"qmlscene"_s},
       {u"qml"_s},
       &tree_sitter_qmljs,
       {u"javascript/highlights.scm"_s, u"typescript/highlights.scm"_s, u"qml/highlights.scm"_s},
       {}});
  add({u"python"_s,
       u"Python"_s,
       {u"py"_s, u"pyw"_s, u"pyi"_s, u"bzl"_s},
       {u"sconstruct"_s, u"sconscript"_s},
       {u"python"_s, u"python2"_s, u"python3"_s},
       {u"py"_s, u"python3"_s},
       &tree_sitter_python,
       {u"python/highlights.scm"_s},
       {}});
  add({u"html"_s,
       u"HTML"_s,
       {u"html"_s, u"htm"_s, u"xhtml"_s},
       {},
       {},
       {u"htm"_s},
       &tree_sitter_html,
       {u"html/highlights.scm"_s},
       {u"html/injections.scm"_s}});
  add({u"xml"_s,
       u"XML"_s,
       {u"xml"_s, u"xsd"_s, u"xsl"_s, u"xslt"_s, u"svg"_s, u"plist"_s, u"rss"_s, u"atom"_s, u"xaml"_s,
        u"ui"_s, u"qrc"_s, u"xliff"_s, u"wsdl"_s, u"csproj"_s, u"vcxproj"_s, u"pom"_s},
       {},
       {},
       {u"svg"_s},
       &tree_sitter_xml,
       {u"xml/highlights.scm"_s},
       {}});
  add({u"markdown"_s,
       u"Markdown"_s,
       {u"md"_s, u"markdown"_s, u"mdown"_s, u"mkd"_s},
       {u"readme"_s},
       {},
       {u"md"_s},
       &tree_sitter_markdown,
       {u"markdown/highlights.scm"_s},
       {u"markdown/injections.scm"_s}});
  LanguageInfo inlineMd{
    u"markdown_inline"_s, u"Markdown (inline)"_s, {}, {}, {}, {},
    &tree_sitter_markdown_inline, {u"markdown_inline/highlights.scm"_s}, {u"markdown_inline/injections.scm"_s}};
  inlineMd.selectable = false;
  add(std::move(inlineMd));
}

const LanguageRegistry &LanguageRegistry::instance() {
  static const LanguageRegistry registry;
  return registry;
}

const LanguageInfo *LanguageRegistry::find(const QString &idOrAlias) const {
  const QString key = idOrAlias.trimmed().toLower();
  if (key.isEmpty())
    return nullptr;
  for (const LanguageInfo &info : m_languages)
    if (info.id == key)
      return &info;
  for (const LanguageInfo &info : m_languages)
    if (info.aliases.contains(key))
      return &info;
  return nullptr;
}

const LanguageInfo *LanguageRegistry::detect(const QString &fileName, const QString &firstLine) const {
  const QString name = QFileInfo(fileName).fileName().toLower();
  if (!name.isEmpty()) {
    for (const LanguageInfo &info : m_languages)
      if (info.selectable && info.fileNames.contains(name))
        return &info;
    const qsizetype dot = name.lastIndexOf(u'.');
    if (dot >= 0) {
      const QString ext = name.mid(dot + 1);
      for (const LanguageInfo &info : m_languages)
        if (info.selectable && info.extensions.contains(ext))
          return &info;
    }
    // README, README.txt ...: extension-less names matched by stem
    const QString stem = dot > 0 ? name.left(dot) : name;
    for (const LanguageInfo &info : m_languages)
      if (info.selectable && dot <= 0 && info.fileNames.contains(stem))
        return &info;
  }
  if (firstLine.startsWith(u"#!"_s)) {
    // "#!/usr/bin/env -S python3 -u" -> python3; "#!/usr/bin/python3.11" -> python3
    QStringList words = firstLine.mid(2).trimmed().split(QRegularExpression(u"\\s+"_s), Qt::SkipEmptyParts);
    if (!words.isEmpty()) {
      QString program = QFileInfo(words.takeFirst()).fileName();
      if (program == u"env"_s) {
        while (!words.isEmpty() && words.first().startsWith(u'-'))
          words.removeFirst();
        program = words.isEmpty() ? QString() : QFileInfo(words.first()).fileName();
      }
      program = program.toLower();
      for (const LanguageInfo &info : m_languages) {
        if (!info.selectable)
          continue;
        for (const QString &interpreter : info.interpreters) {
          if (program == interpreter || (program.startsWith(interpreter) && program.mid(interpreter.size()).at(0) == u'.'))
            return &info;
        }
      }
    }
  }
  return nullptr;
}

std::shared_ptr<const CompiledLanguage> LanguageRegistry::compiled(const QString &id) const {
  static QMutex mutex;
  static std::map<QString, std::shared_ptr<const CompiledLanguage>> cache;
  const LanguageInfo *info = find(id);
  if (!info)
    return nullptr;
  QMutexLocker lock(&mutex);
  if (auto it = cache.find(info->id); it != cache.end())
    return it->second;
  auto result = std::make_shared<CompiledLanguage>();
  result->info = info;
  result->language = info->grammar();
  auto build = [&](const QStringList &files) -> TSQuery * {
    QString source;
    for (const QString &file : files) {
      source += readResource(file);
      source += u'\n';
    }
    return source.trimmed().isEmpty() ? nullptr : compileQuery(result->language, source, &result->warnings);
  };
  result->highlights = build(info->highlightQueries);
  result->injections = build(info->injectionQueries);
  if (result->highlights)
    result->highlightInfo = QueryInfo::analyze(result->highlights);
  if (result->injections)
    result->injectionInfo = QueryInfo::analyze(result->injections);
  cache.emplace(info->id, result);
  return result;
}

} // namespace qce
