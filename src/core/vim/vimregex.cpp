#include "core/vim/vimregex.h"

using namespace Qt::StringLiterals;

namespace qce::vim {

namespace {

enum class Magic { VeryMagic, Magic, NoMagic, VeryNoMagic };

// Is the unescaped `c` special in this mode (as opposed to the escaped one)?
bool specialUnescaped(Magic mode, QChar c) {
  switch (mode) {
  case Magic::VeryMagic:
    return QStringView(u"()|+?={}<>@%&^$.*[~").contains(c);
  case Magic::Magic:
    return QStringView(u"^$.*[~").contains(c);
  case Magic::NoMagic:
    return c == u'^' || c == u'$';
  case Magic::VeryNoMagic:
    return false;
  }
  return false;
}

// Does the backslash-escaped `c` mean the special thing in this mode?
bool specialEscaped(Magic mode, QChar c) {
  switch (mode) {
  case Magic::VeryMagic:
    return false;
  case Magic::Magic:
    return QStringView(u"()|+?={}<>@%&").contains(c);
  case Magic::NoMagic:
    return QStringView(u"()|+?={}<>@%&.*[~").contains(c);
  case Magic::VeryNoMagic:
    return QStringView(u"()|+?={}<>@%&.*[~^$").contains(c);
  }
  return false;
}

QString literal(QChar c) { return QRegularExpression::escape(QString(c)); }

} // namespace

CompiledPattern compilePattern(const QString &pattern, PatternOptions options) {
  CompiledPattern result;
  QString out;
  Magic mode = Magic::Magic;
  bool forceCase = false, forceIgnore = false;
  bool hasUpper = false;
  const qsizetype n = pattern.size();
  auto fail = [&](const QString &message) {
    result.error = message;
    return result;
  };

  // Is the position at the start of a branch (pattern start, after \( \| \%( or \&)?
  auto atBranchStart = [&](qsizetype i) {
    if (i == 0)
      return true;
    const QStringView before = QStringView(pattern).left(i);
    return before.endsWith(u"\\("_s) || before.endsWith(u"\\|"_s) || before.endsWith(u"\\%("_s) ||
           (mode == Magic::VeryMagic && (before.endsWith(u"("_s) || before.endsWith(u"|"_s)));
  };
  auto atBranchEnd = [&](qsizetype i) { // i is just past the '$'
    if (i >= n)
      return true;
    const QStringView rest = QStringView(pattern).mid(i);
    return rest.startsWith(u"\\)"_s) || rest.startsWith(u"\\|"_s) ||
           (mode == Magic::VeryMagic && (rest.startsWith(u")"_s) || rest.startsWith(u"|"_s)));
  };

  for (qsizetype i = 0; i < n; ++i) {
    const QChar c = pattern[i];
    bool escaped = false;
    if (c.isUpper())
      hasUpper = true;
    if (c == u'\\') {
      if (i + 1 >= n) {
        out += u"\\\\"_s;
        break;
      }
      const QChar e = pattern[++i];
      // Flags that change how the rest is read.
      if (e == u'v') {
        mode = Magic::VeryMagic;
        continue;
      }
      if (e == u'm') {
        mode = Magic::Magic;
        continue;
      }
      if (e == u'M') {
        mode = Magic::NoMagic;
        continue;
      }
      if (e == u'V') {
        mode = Magic::VeryNoMagic;
        continue;
      }
      if (e == u'c') {
        forceIgnore = true;
        continue;
      }
      if (e == u'C') {
        forceCase = true;
        continue;
      }
      if (specialEscaped(mode, e)) {
        // The escaped form is the special one in this mode.
        escaped = true;
        goto special;
      }
      // Escaped special characters of the other kinds are literals ("\." in magic mode).
      if (specialUnescaped(mode, e) && e != u'^' && e != u'$') {
        out += literal(e);
        continue;
      }
      switch (e.unicode()) {
      case 's':
        out += u"[ \\t]"_s;
        continue;
      case 'S':
        out += u"[^ \\t]"_s;
        continue;
      case 'd':
        out += u"\\d"_s;
        continue;
      case 'D':
        out += u"\\D"_s;
        continue;
      case 'w':
        out += u"\\w"_s;
        continue;
      case 'W':
        out += u"\\W"_s;
        continue;
      case 'a':
        out += u"[A-Za-z]"_s;
        continue;
      case 'A':
        out += u"[^A-Za-z]"_s;
        continue;
      case 'l':
        out += u"[a-z]"_s;
        continue;
      case 'L':
        out += u"[^a-z]"_s;
        continue;
      case 'u':
        out += u"[A-Z]"_s;
        continue;
      case 'U':
        out += u"[^A-Z]"_s;
        continue;
      case 'h':
        out += u"[A-Za-z_]"_s;
        continue;
      case 'H':
        out += u"[^A-Za-z_]"_s;
        continue;
      case 'x':
        out += u"[0-9A-Fa-f]"_s;
        continue;
      case 'X':
        out += u"[^0-9A-Fa-f]"_s;
        continue;
      case 'o':
        out += u"[0-7]"_s;
        continue;
      case 'O':
        out += u"[^0-7]"_s;
        continue;
      case 'i':
      case 'k':
        out += u"[\\w]"_s;
        continue;
      case 'f':
      case 'p':
        out += u"[^ \\t]"_s;
        continue;
      case 't':
        out += u"\\t"_s;
        continue;
      case 'e':
        out += u"\\x1b"_s;
        continue;
      case 'r':
        out += u"\\r"_s;
        continue;
      case 'n':
        return fail(u"E486: a pattern with \\n (multi-line) is not supported"_s);
      case 'z':
        if (i + 1 < n && pattern[i + 1] == u's') {
          out += u"\\K"_s;
          ++i;
          continue;
        }
        return fail(u"E68: \\z is not supported"_s);
      case '_':
        return fail(u"E486: \\_ (multi-line) is not supported"_s);
      case '^':
        out += (mode == Magic::VeryNoMagic && atBranchStart(i - 1)) ? u"^"_s : u"\\^"_s;
        continue;
      case '$':
        out += u"\\$"_s;
        continue;
      default:
        break;
      }
      if (e >= u'1' && e <= u'9') {
        out += u"\\"_s + e;
        continue;
      }
      if (e == u'0') {
        out += u"\\0"_s;
        continue;
      }
      out += literal(e);
      continue;
    }
    if (specialUnescaped(mode, c)) {
      goto special;
    }
    out += literal(c);
    continue;

  special:
    // `c` for the unescaped specials, `pattern[i]` (after the backslash) for escaped ones: both are the
    // character at i here.
    {
      const QChar s = pattern[i];
      switch (s.unicode()) {
      case '^':
        out += atBranchStart(escaped ? i - 1 : i) ? u"^"_s : u"\\^"_s;
        break;
      case '$':
        out += atBranchEnd(i + 1) ? u"$"_s : u"\\$"_s;
        break;
      case '.':
        out += u"."_s;
        break;
      case '*':
        out += u"*"_s;
        break;
      case '+':
        out += u"+"_s;
        break;
      case '?':
      case '=':
        out += u"?"_s;
        break;
      case '(':
        out += u"("_s;
        break;
      case ')':
        out += u")"_s;
        break;
      case '|':
        out += u"|"_s;
        break;
      case '<':
        out += u"\\b(?=\\w)"_s;
        break;
      case '>':
        out += u"\\b(?<=\\w)"_s;
        break;
      case '~':
        out += u"~"_s;
        break; // the previous substitute string is not tracked
      case '&':
        out += u"&"_s;
        break;
      case '%':
        if (i + 1 < n && pattern[i + 1] == u'(') {
          out += u"(?:"_s;
          ++i;
          break;
        }
        return fail(u"E71: invalid character after \\%"_s);
      case '@':
        return fail(u"E64: look-around (\\@) is not supported"_s);
      case '{': {
        // \{n,m}, \{n}, \{-n,m}, \{} : up to a closing } or \}
        qsizetype close = pattern.indexOf(u'}', i + 1);
        if (close < 0)
          return fail(u"E554: syntax error in \\{...}"_s);
        QString body = pattern.mid(i + 1, close - i - 1);
        if (body.endsWith(u'\\'))
          body.chop(1);
        bool lazy = false;
        if (body.startsWith(u'-')) {
          lazy = true;
          body.remove(0, 1);
        }
        if (body.isEmpty())
          out += lazy ? u"*?"_s : u"*"_s;
        else if (body.startsWith(u','))
          out += u"{0"_s + body + u"}"_s + (lazy ? u"?"_s : QString());
        else
          out += u"{"_s + body + u"}"_s + (lazy ? u"?"_s : QString());
        i = close;
        break;
      }
      case '[': {
        // A collection runs to the matching ']'; without one the '[' is a literal.
        qsizetype j = i + 1;
        QString set = u"["_s;
        if (j < n && pattern[j] == u'^') {
          set += u'^';
          ++j;
        }
        if (j < n && pattern[j] == u']') {
          set += u"\\]"_s;
          ++j;
        }
        bool closed = false;
        for (; j < n; ++j) {
          const QChar d = pattern[j];
          if (d == u']') {
            closed = true;
            break;
          }
          if (d == u'[' && j + 1 < n && pattern[j + 1] == u':') {
            const qsizetype end = pattern.indexOf(u":]"_s, j + 2);
            if (end > 0) {
              set += QStringView(pattern).mid(j, end + 2 - j);
              j = end + 1;
              continue;
            }
          }
          if (d == u'\\' && j + 1 < n) {
            const QChar x = pattern[++j];
            switch (x.unicode()) {
            case 'e':
              set += u"\\x1b"_s;
              break;
            case 't':
              set += u"\\t"_s;
              break;
            case 'r':
              set += u"\\r"_s;
              break;
            case 'b':
              set += u"\\x08"_s;
              break;
            case '\\':
              set += u"\\\\"_s;
              break;
            case ']':
              set += u"\\]"_s;
              break;
            case '^':
              set += u"\\^"_s;
              break;
            case '-':
              set += u"\\-"_s;
              break;
            case 'n':
              return fail(u"E486: \\n in [] is not supported"_s);
            default:
              set += u"\\\\"_s + literal(x);
              break;
            }
            continue;
          }
          if (d == u'[')
            set += u"\\["_s;
          else if (d == u'^')
            set += u"\\^"_s;
          else
            set += d;
        }
        if (!closed) {
          out += u"\\["_s;
        } else {
          out += set + u"]"_s;
          i = j;
        }
        break;
      }
      default:
        out += literal(s);
        break;
      }
    }
  }

  QRegularExpression::PatternOptions opts;
  bool ignore = options.ignoreCase;
  if (ignore && options.smartCase && hasUpper)
    ignore = false;
  if (forceIgnore)
    ignore = true;
  if (forceCase)
    ignore = false;
  if (ignore)
    opts |= QRegularExpression::CaseInsensitiveOption;
  result.caseSensitive = !ignore;
  {
    // Plain text (nothing special in magic mode) with optional \< \> around it.
    QString body = pattern;
    bool word = false;
    if (body.startsWith(u"\\<"_s) && body.endsWith(u"\\>"_s) && body.size() > 4) {
      body = body.mid(2, body.size() - 4);
      word = true;
    }
    static const QString special = QStringLiteral("\\^$.*[~");
    bool plain = !body.isEmpty() && mode == Magic::Magic;
    for (QChar c : body)
      plain = plain && !special.contains(c);
    if (plain && !forceCase && !forceIgnore) {
      result.literal = body;
      result.wholeWord = word;
    }
  }
  result.regex = QRegularExpression(out, opts);
  if (!result.regex.isValid())
    result.error = u"E486: invalid pattern: "_s + result.regex.errorString();
  return result;
}

QString expandReplacement(const QString &rep, const QRegularExpressionMatch &match) {
  QString out;
  enum class Case { None, Upper, Lower } mode = Case::None;
  enum class Once { None, Upper, Lower } once = Once::None;
  auto append = [&](const QString &text) {
    for (QChar c : text) {
      if (once == Once::Upper) {
        out += c.toUpper();
        once = Once::None;
      } else if (once == Once::Lower) {
        out += c.toLower();
        once = Once::None;
      } else if (mode == Case::Upper) {
        out += c.toUpper();
      } else if (mode == Case::Lower) {
        out += c.toLower();
      } else {
        out += c;
      }
    }
  };
  for (qsizetype i = 0; i < rep.size(); ++i) {
    const QChar c = rep[i];
    if (c == u'&') {
      append(match.captured(0));
    } else if (c == u'\\' && i + 1 < rep.size()) {
      const QChar e = rep[++i];
      if (e >= u'0' && e <= u'9')
        append(match.captured(e.digitValue()));
      else if (e == u'r' || e == u'n')
        out += u'\n';
      else if (e == u't')
        out += u'\t';
      else if (e == u'u')
        once = Once::Upper;
      else if (e == u'l')
        once = Once::Lower;
      else if (e == u'U')
        mode = Case::Upper;
      else if (e == u'L')
        mode = Case::Lower;
      else if (e == u'e' || e == u'E')
        mode = Case::None;
      else
        append(QString(e));
    } else {
      append(QString(c));
    }
  }
  return out;
}

} // namespace qce::vim
