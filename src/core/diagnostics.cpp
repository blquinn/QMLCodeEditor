#include "core/diagnostics.h"

#include <algorithm>

namespace qce {

namespace {

TextPosition positionFromLsp(const QVariant &value) {
  const QVariantMap map = value.toMap();
  return {qMax<qsizetype>(0, map.value(QStringLiteral("line")).toLongLong()),
          qMax<qsizetype>(0, map.value(QStringLiteral("character")).toLongLong())};
}

QVariantMap positionToLsp(TextPosition position) {
  return {{QStringLiteral("line"), qlonglong(position.line)}, {QStringLiteral("character"), qlonglong(position.column)}};
}

QVariantMap rangeToLsp(TextPosition start, TextPosition end) {
  return {{QStringLiteral("start"), positionToLsp(start)}, {QStringLiteral("end"), positionToLsp(end)}};
}

int clampSeverity(int severity) { return severity >= ErrorSeverity && severity <= HintSeverity ? severity : ErrorSeverity; }

constexpr quint32 kPrimaryKinds =
  decorationKindBit(DecorationKind::Squiggle) | decorationKindBit(DecorationKind::Underline);

} // namespace

Diagnostic Diagnostic::fromLsp(const QVariantMap &map) {
  Diagnostic d;
  const QVariantMap range = map.value(QStringLiteral("range")).toMap();
  d.start = positionFromLsp(range.value(QStringLiteral("start")));
  d.end = positionFromLsp(range.value(QStringLiteral("end")));
  d.severity = clampSeverity(map.value(QStringLiteral("severity"), ErrorSeverity).toInt());
  d.message = map.value(QStringLiteral("message")).toString();
  d.code = map.value(QStringLiteral("code"));
  d.source = map.value(QStringLiteral("source")).toString();
  d.data = map.value(QStringLiteral("data"));
  const QVariantList related = map.value(QStringLiteral("relatedInformation")).toList();
  for (const QVariant &item : related) {
    const QVariantMap info = item.toMap();
    const QVariantMap location = info.value(QStringLiteral("location")).toMap();
    const QVariantMap infoRange = location.value(QStringLiteral("range")).toMap();
    d.related.append({location.value(QStringLiteral("uri")).toString(),
                      positionFromLsp(infoRange.value(QStringLiteral("start"))),
                      positionFromLsp(infoRange.value(QStringLiteral("end"))), info.value(QStringLiteral("message")).toString()});
  }
  const QVariantList tags = map.value(QStringLiteral("tags")).toList();
  for (const QVariant &tag : tags)
    d.tags.append(tag.toInt());
  return d;
}

QVariantMap Diagnostic::toLsp() const {
  QVariantMap map{{QStringLiteral("range"), rangeToLsp(start, end)},
                  {QStringLiteral("severity"), severity},
                  {QStringLiteral("message"), message}};
  if (code.isValid())
    map.insert(QStringLiteral("code"), code);
  if (!source.isEmpty())
    map.insert(QStringLiteral("source"), source);
  if (!related.isEmpty()) {
    QVariantList list;
    for (const DiagnosticRelated &info : related)
      list.append(QVariantMap{
        {QStringLiteral("location"),
         QVariantMap{{QStringLiteral("uri"), info.uri}, {QStringLiteral("range"), rangeToLsp(info.start, info.end)}}},
        {QStringLiteral("message"), info.message}
      });
    map.insert(QStringLiteral("relatedInformation"), list);
  }
  if (!tags.isEmpty()) {
    QVariantList list;
    for (int tag : tags)
      list.append(tag);
    map.insert(QStringLiteral("tags"), list);
  }
  if (data.isValid())
    map.insert(QStringLiteral("data"), data);
  return map;
}

DiagnosticSet::DiagnosticSet(TextDocument *document, DecorationSet *decorations, QObject *parent)
    : QObject(parent), m_document(document), m_decorations(decorations) {
  // The decoration set empties its layers on a reset; the list goes with them.
  connect(document, &TextDocument::textReset, this, [this] {
    if (m_items.isEmpty())
      return;
    m_items.clear();
    emit changed();
  });
}

qsizetype DiagnosticSet::offsetOf(TextPosition position) const { return m_document->rope().offsetAt(position); }

bool DiagnosticSet::isPrimary(const Decoration &d) {
  return d.layer == kLayer && (decorationKindBit(d.kind) & kPrimaryKinds);
}

void DiagnosticSet::rebuild() {
  QList<DecorationSpec> specs;
  specs.reserve(m_items.size() * (m_endOfLine ? 3 : 2));
  for (qsizetype i = 0; i < m_items.size(); ++i) {
    const Diagnostic &d = m_items[i];
    const qsizetype start = offsetOf(d.start);
    const qsizetype end = qMax(start, offsetOf(d.end));
    const int severity = clampSeverity(d.severity);
    DecorationSpec spec;
    spec.start = start;
    spec.end = end;
    spec.kind = severity == HintSeverity ? DecorationKind::Underline : DecorationKind::Squiggle;
    spec.severity = severity;
    spec.priority = HintSeverity + 1 - severity; // errors paint over warnings, and win the gutter
    spec.tag = int(i);
    specs.append(spec);
    // The icon and the message belong to where the diagnostic starts, so they take no range.
    spec.end = start;
    spec.kind = DecorationKind::GutterIcon;
    specs.append(spec);
    if (m_endOfLine) {
      spec.kind = DecorationKind::EndOfLineText;
      spec.text = d.message;
      specs.append(spec);
    }
  }
  m_decorations->setLayer(kLayer, specs);
}

void DiagnosticSet::setDiagnostics(const QList<Diagnostic> &diagnostics) {
  m_items = diagnostics;
  rebuild();
  emit changed();
}

void DiagnosticSet::clear() {
  if (m_items.isEmpty())
    return;
  m_items.clear();
  m_decorations->clearLayer(kLayer);
  emit changed();
}

void DiagnosticSet::setEndOfLineMessages(bool show) {
  if (show == m_endOfLine)
    return;
  m_endOfLine = show;
  if (!m_items.isEmpty())
    rebuild();
}

Diagnostic DiagnosticSet::withCurrentRange(const Decoration &decoration) const {
  Diagnostic d = m_items.at(decoration.tag);
  const Rope &rope = m_document->rope();
  d.start = rope.positionAt(decoration.start);
  d.end = rope.positionAt(decoration.end);
  return d;
}

std::optional<Diagnostic> DiagnosticSet::diagnosticFor(const Decoration &decoration) const {
  if (decoration.layer != kLayer || decoration.tag < 0 || decoration.tag >= m_items.size())
    return std::nullopt;
  return withCurrentRange(decoration);
}

QList<Diagnostic> DiagnosticSet::at(qsizetype offset) const {
  QList<Diagnostic> out;
  if (m_items.isEmpty())
    return out;
  for (const Decoration &d : m_decorations->query(offset, offset, kPrimaryKinds))
    if (isPrimary(d) && d.tag < m_items.size() && d.start <= offset && (offset < d.end || d.start == d.end))
      out.append(withCurrentRange(d));
  std::stable_sort(out.begin(), out.end(), [](const Diagnostic &a, const Diagnostic &b) { return a.severity < b.severity; });
  return out;
}

QList<Diagnostic> DiagnosticSet::inRange(qsizetype firstOffset, qsizetype lastOffset) const {
  QList<Diagnostic> out;
  if (m_items.isEmpty())
    return out;
  for (const Decoration &d : m_decorations->query(firstOffset, lastOffset, kPrimaryKinds))
    if (isPrimary(d) && d.tag < m_items.size())
      out.append(withCurrentRange(d));
  return out;
}

std::optional<Diagnostic> DiagnosticSet::next(qsizetype offset, bool wrap, int leastSevere) const {
  if (m_items.isEmpty())
    return std::nullopt;
  auto accept = [&](const Decoration &d) { return isPrimary(d) && d.severity <= leastSevere; };
  auto found = m_decorations->next(offset, accept);
  if (!found && wrap)
    found = m_decorations->next(-1, accept);
  return found ? std::optional(withCurrentRange(*found)) : std::nullopt;
}

std::optional<Diagnostic> DiagnosticSet::previous(qsizetype offset, bool wrap, int leastSevere) const {
  if (m_items.isEmpty())
    return std::nullopt;
  auto accept = [&](const Decoration &d) { return isPrimary(d) && d.severity <= leastSevere; };
  auto found = m_decorations->previous(offset, accept);
  if (!found && wrap)
    found = m_decorations->previous(m_document->length() + 1, accept);
  return found ? std::optional(withCurrentRange(*found)) : std::nullopt;
}

} // namespace qce
