#include "core/vim/vimkeys.h"

#include <QtCore/QHash>

using namespace Qt::StringLiterals;

namespace qce::vim {

namespace {

struct Named {
  Qt::Key key;
  const char *name;
};

constexpr Named kNamed[] = {
  {Qt::Key_Escape, "Esc"},        {Qt::Key_Return, "CR"},
  {Qt::Key_Enter, "CR"},          {Qt::Key_Backspace, "BS"},
  {Qt::Key_Tab, "Tab"},           {Qt::Key_Delete, "Del"},
  {Qt::Key_Insert, "Insert"},     {Qt::Key_Home, "Home"},
  {Qt::Key_End, "End"},           {Qt::Key_PageUp, "PageUp"},
  {Qt::Key_PageDown, "PageDown"}, {Qt::Key_Left, "Left"},
  {Qt::Key_Right, "Right"},       {Qt::Key_Up, "Up"},
  {Qt::Key_Down, "Down"},
};

bool printableText(const QString &text) {
  if (text.isEmpty())
    return false;
  for (QChar c : text)
    if (c.unicode() < 0x20 || c.unicode() == 0x7f)
      return false;
  return true;
}

} // namespace

bool isPrintableSymbol(const QString &symbol) {
  if (symbol.isEmpty() || (symbol.size() > 1 && !(symbol.size() == 2 && symbol[0].isHighSurrogate())))
    return false;
  return symbol.size() == 2 || (symbol[0].unicode() >= 0x20 && symbol[0].unicode() != 0x7f);
}

QString keySymbol(const QKeyEvent *event) {
  const Qt::KeyboardModifiers mods = event->modifiers() & ~Qt::KeypadModifier;
  const bool ctrl = mods & Qt::ControlModifier, alt = mods & Qt::AltModifier,
             shift = mods & Qt::ShiftModifier;
  if (mods & Qt::MetaModifier)
    return {};
  const QString text = event->text();
  const int key = event->key();

  // AltGr arrives as Ctrl+Alt with the character in the text.
  if (ctrl && alt && printableText(text))
    return text;

  if (key == Qt::Key_Backtab)
    return u"<S-Tab>"_s;
  for (const Named &named : kNamed) {
    if (named.key != key)
      continue;
    QString prefix;
    if (ctrl)
      prefix += u"C-"_s;
    if (alt)
      prefix += u"A-"_s;
    if (
      shift && key != Qt::Key_Return && key != Qt::Key_Enter && key != Qt::Key_Escape &&
      key != Qt::Key_Backspace
    )
      prefix += u"S-"_s;
    return u"<"_s + prefix + QLatin1StringView(named.name) + u">"_s;
  }

  if (ctrl) {
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
      return u"<C-"_s + (shift ? u"S-"_s : QString()) + QChar(u'a' + (key - Qt::Key_A)) + u">"_s;
    if (key == Qt::Key_BracketLeft)
      return u"<Esc>"_s;
    if (key == Qt::Key_Space)
      return u"<C-Space>"_s;
    if (key == Qt::Key_6 || key == Qt::Key_AsciiCircum)
      return u"<C-^>"_s;
    return {};
  }
  if (alt) {
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
      return u"<A-"_s + QChar(u'a' + (key - Qt::Key_A)) + u">"_s;
    if (printableText(text))
      return u"<A-"_s + text + u">"_s;
    return {};
  }
  if (printableText(text))
    return text;
  return {};
}

QStringList parseKeys(QStringView notation) {
  QStringList out;
  const qsizetype n = notation.size();
  for (qsizetype i = 0; i < n;) {
    if (notation[i] == u'<') {
      const qsizetype close = notation.indexOf(u'>', i + 1);
      if (close > i + 1 && close - i <= 12) {
        const QString inner = notation.mid(i + 1, close - i - 1).toString();
        const QString lower = inner.toLower();
        QString symbol;
        if (lower == u"lt"_s)
          symbol = u"<"_s;
        else if (lower == u"space"_s)
          symbol = u" "_s;
        else if (lower == u"bar"_s)
          symbol = u"|"_s;
        else if (lower == u"bslash"_s)
          symbol = u"\\"_s;
        else if (lower == u"cr"_s || lower == u"enter"_s || lower == u"return"_s)
          symbol = u"<CR>"_s;
        else if (lower == u"esc"_s)
          symbol = u"<Esc>"_s;
        else if (lower == u"bs"_s)
          symbol = u"<BS>"_s;
        else if (lower == u"tab"_s)
          symbol = u"<Tab>"_s;
        else if (lower == u"s-tab"_s)
          symbol = u"<S-Tab>"_s;
        else if (lower == u"del"_s || lower == u"delete"_s)
          symbol = u"<Del>"_s;
        else if (lower == u"insert"_s)
          symbol = u"<Insert>"_s;
        else if (lower == u"home"_s)
          symbol = u"<Home>"_s;
        else if (lower == u"end"_s)
          symbol = u"<End>"_s;
        else if (lower == u"pageup"_s)
          symbol = u"<PageUp>"_s;
        else if (lower == u"pagedown"_s)
          symbol = u"<PageDown>"_s;
        else if (lower == u"up"_s)
          symbol = u"<Up>"_s;
        else if (lower == u"down"_s)
          symbol = u"<Down>"_s;
        else if (lower == u"left"_s)
          symbol = u"<Left>"_s;
        else if (lower == u"right"_s)
          symbol = u"<Right>"_s;
        else if (
          lower.size() >= 3 &&
          (lower.startsWith(u"c-"_s) || lower.startsWith(u"a-"_s) || lower.startsWith(u"m-"_s))
        ) {
          const QString rest = inner.mid(2);
          QString restSymbol = rest.size() == 1 ? rest.toLower() : QString();
          if (restSymbol.isEmpty() && rest.compare(u"space"_s, Qt::CaseInsensitive) == 0)
            restSymbol = u"Space"_s;
          if (restSymbol.isEmpty())
            for (const Named &named : kNamed)
              if (rest.compare(QLatin1StringView(named.name), Qt::CaseInsensitive) == 0)
                restSymbol = QLatin1StringView(named.name);
          if (!restSymbol.isEmpty()) {
            const QChar kind = lower[0] == u'm' ? u'A' : lower[0].toUpper();
            symbol = u"<"_s + kind + u"-"_s + restSymbol + u">"_s;
            if (symbol == u"<C-[>"_s)
              symbol = u"<Esc>"_s;
          }
        }
        if (!symbol.isEmpty()) {
          out.append(symbol);
          i = close + 1;
          continue;
        }
      }
    }
    const qsizetype len = notation[i].isHighSurrogate() && i + 1 < n ? 2 : 1;
    out.append(notation.mid(i, len).toString());
    i += len;
  }
  return out;
}

QString formatKeys(const QStringList &keys) {
  QString out;
  for (const QString &key : keys) {
    if (key == u"<"_s)
      out += u"<lt>"_s;
    else
      out += key;
  }
  return out;
}

} // namespace qce::vim
