#ifndef QCE_TEXTPOSITION_H
#define QCE_TEXTPOSITION_H

#include <QtCore/QtGlobal>

#include <compare>

namespace qce {

// Zero-based line and UTF-16 column, as in LSP's Position (ADR 0007).
struct TextPosition {
  qsizetype line = 0;
  qsizetype column = 0;

  friend constexpr bool operator==(const TextPosition &, const TextPosition &) = default;
  friend constexpr auto operator<=>(const TextPosition &, const TextPosition &) = default;
};

} // namespace qce

#endif // QCE_TEXTPOSITION_H
