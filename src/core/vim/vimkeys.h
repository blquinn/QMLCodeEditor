#ifndef QCE_VIM_VIMKEYS_H
#define QCE_VIM_VIMKEYS_H

#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtGui/QKeyEvent>

namespace qce::vim {

// Vim reads keys as symbols: a printable character is its own text ("d", "<" , " "), everything else
// is spelled in vim's notation ("<Esc>", "<CR>", "<C-d>", "<Up>"). Macros, dot-repeat and the test
// harness all work on symbol lists, so a register holding a recorded macro is readable text.

// The symbol for a key event, or an empty string when vim has no use for it.
QString keySymbol(const QKeyEvent *event);

// "d2w<Esc>" -> {"d", "2", "w", "<Esc>"}. Understands <lt>, <Space>, <Bar>, <Bslash>, <CR>, <Enter>,
// <Esc>, <BS>, <Tab>, <S-Tab>, <Del>, arrows, <Home>/<End>/<PageUp>/<PageDown>/<Insert>, <C-x>, <A-x>.
// Anything that is not a valid <...> is taken literally.
QStringList parseKeys(QStringView notation);

// The inverse: joins symbols into notation ("<" becomes "<lt>").
QString formatKeys(const QStringList &keys);

// Is the symbol one printable character (one code point)?
bool isPrintableSymbol(const QString &symbol);

} // namespace qce::vim

#endif // QCE_VIM_VIMKEYS_H
