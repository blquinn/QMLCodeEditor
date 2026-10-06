#ifndef QCE_INLAYHINTS_H
#define QCE_INLAYHINTS_H

#include "core/decorationset.h"
#include "core/rope.h"
#include "core/textposition.h"

#include <QtCore/QList>
#include <QtCore/QString>
#include <QtCore/QVariant>

namespace qce {

// The decoration layer inlay hints live in.
constexpr int kInlayLayer = -2;

// LSP's InlayHintKind.
enum InlayHintKind { TypeHint = 1, ParameterHint = 2 };

// An inlay hint as a language server sends it (DIAG-06): a label shown inline at a position.
struct InlayHint {
  TextPosition position;
  QString label; // a string, or the parts of a label joined
  int kind = 0;  // InlayHintKind; 0 when the server gave none
  bool paddingLeft = false;
  bool paddingRight = false;
  QVariant data;

  // From LSP's JSON shape as a variant map ({position: {line, character}, label: string | [{value}],
  // kind, paddingLeft, paddingRight, data}).
  static InlayHint fromLsp(const QVariantMap &map);
};

// Inline decorations for `hints`, ready for DecorationSet::setLayer(kInlayLayer, ...). A parameter
// hint leans on the text after it (typing at its position goes after the hint) and any other on
// the text before it, so a type annotation stays with the name it follows. The label gets a space
// on each side (two with LSP's padding flags), which is the padding of the pill drawn behind it.
// Positions outside the text are clamped; the decoration's tag is the hint's index.
QList<DecorationSpec> inlayHintSpecs(const QList<InlayHint> &hints, const Rope &rope);

} // namespace qce

#endif // QCE_INLAYHINTS_H
