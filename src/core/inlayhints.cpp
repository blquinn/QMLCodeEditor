#include "core/inlayhints.h"

namespace qce {

InlayHint InlayHint::fromLsp(const QVariantMap &map) {
  InlayHint hint;
  const QVariantMap position = map.value(QStringLiteral("position")).toMap();
  hint.position = {qMax<qsizetype>(0, position.value(QStringLiteral("line")).toLongLong()),
                   qMax<qsizetype>(0, position.value(QStringLiteral("character")).toLongLong())};
  const QVariant label = map.value(QStringLiteral("label"));
  if (label.typeId() == QMetaType::QVariantList) {
    for (const QVariant &part : label.toList())
      hint.label += part.toMap().value(QStringLiteral("value")).toString();
  } else {
    hint.label = label.toString();
  }
  hint.kind = map.value(QStringLiteral("kind")).toInt();
  hint.paddingLeft = map.value(QStringLiteral("paddingLeft")).toBool();
  hint.paddingRight = map.value(QStringLiteral("paddingRight")).toBool();
  hint.data = map.value(QStringLiteral("data"));
  return hint;
}

QList<DecorationSpec> inlayHintSpecs(const QList<InlayHint> &hints, const Rope &rope) {
  QList<DecorationSpec> specs;
  specs.reserve(hints.size());
  for (qsizetype i = 0; i < hints.size(); ++i) {
    const InlayHint &hint = hints[i];
    if (hint.label.isEmpty())
      continue;
    DecorationSpec spec;
    spec.start = spec.end = rope.offsetAt(hint.position);
    spec.kind = DecorationKind::InlineText;
    spec.text = QString(hint.paddingLeft ? 2 : 1, u' ') + hint.label + QString(hint.paddingRight ? 2 : 1, u' ');
    spec.startGravity = hint.kind == ParameterHint ? Gravity::Left : Gravity::Right;
    spec.tag = int(i);
    specs.append(spec);
  }
  return specs;
}

} // namespace qce
