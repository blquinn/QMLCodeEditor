#ifndef QCE_DIAGNOSTICS_H
#define QCE_DIAGNOSTICS_H

#include "core/decorationset.h"
#include "core/textdocument.h"
#include "core/textposition.h"

#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVariant>

#include <optional>

namespace qce {

// LSP's DiagnosticTag.
enum DiagnosticTag { UnnecessaryTag = 1, DeprecatedTag = 2 };

// LSP's DiagnosticRelatedInformation: a message about another place.
struct DiagnosticRelated {
  QString uri;
  TextPosition start;
  TextPosition end;
  QString message;
};

// A diagnostic as a language server sends it (ADR 0016): everything of LSP's Diagnostic that the
// editor can show or a host may want back. Positions are line and UTF-16 column, LSP's default.
struct Diagnostic {
  TextPosition start;
  TextPosition end;
  int severity = ErrorSeverity; // DecorationSeverity, which shares LSP's numbering
  QString message;
  QVariant code; // a number or a string
  QString source;
  QList<DiagnosticRelated> related;
  QList<int> tags; // DiagnosticTag
  QVariant data;   // handed back untouched (code actions use it)

  // From and to LSP's JSON shape as a variant map ({range: {start: {line, character}, end: ...},
  // severity, message, code, source, relatedInformation: [{location: {uri, range}, message}], tags,
  // data}), so a host can forward publishDiagnostics as it is. A missing severity counts as an error.
  static Diagnostic fromLsp(const QVariantMap &map);
  QVariantMap toLsp() const;
};

// The document's diagnostics (DIAG-03): keeps the list a host pushed and shows it through the
// editor's DecorationSet, in one layer of its own, so the ranges follow edits and the usual
// rendering (squiggles, gutter icons, end-of-line messages) applies. Errors, warnings and
// information are squiggles; hints are plain underlines. Every diagnostic gets a gutter icon, and
// with end-of-line messages on, its message after its line.
//
// Positions outside the text are clamped to it. Replacing the list replaces the layer in one call, so
// pushing 100k diagnostics is a sort and a merge, not 100k inserts.
class DiagnosticSet : public QObject {
  Q_OBJECT
public:
  // The decoration layer the diagnostics live in.
  static constexpr int kLayer = -1;

  DiagnosticSet(TextDocument *document, DecorationSet *decorations, QObject *parent = nullptr);

  qsizetype count() const { return m_items.size(); }
  void setDiagnostics(const QList<Diagnostic> &diagnostics);
  void clear();

  // Show each diagnostic's message after the end of the line it starts on.
  bool endOfLineMessages() const { return m_endOfLine; }
  void setEndOfLineMessages(bool show);

  // The diagnostics whose range contains the character at `offset` (an empty range at `offset`
  // counts), most severe first, with their ranges as they are now.
  QList<Diagnostic> at(qsizetype offset) const;
  // The diagnostics touching [firstOffset, lastOffset], in start order, ranges as they are now.
  QList<Diagnostic> inRange(qsizetype firstOffset, qsizetype lastOffset) const;
  // The diagnostic a decoration of this set stands for, with its current range; nullopt when the
  // decoration is not one of ours.
  std::optional<Diagnostic> diagnosticFor(const Decoration &decoration) const;

  // The first diagnostic starting after `offset` (or the last starting before it), at least as severe
  // as `leastSevere`; with `wrap` it continues from the other end of the text. nullopt when there is none.
  std::optional<Diagnostic> next(qsizetype offset, bool wrap = true, int leastSevere = HintSeverity) const;
  std::optional<Diagnostic> previous(qsizetype offset, bool wrap = true, int leastSevere = HintSeverity) const;

signals:
  void changed();

private:
  static bool isPrimary(const Decoration &decoration);
  qsizetype offsetOf(TextPosition position) const;
  Diagnostic withCurrentRange(const Decoration &decoration) const;
  void rebuild();

  TextDocument *m_document;
  DecorationSet *m_decorations;
  QList<Diagnostic> m_items; // a decoration's tag is an index into this list
  bool m_endOfLine = false;
};

} // namespace qce

#endif // QCE_DIAGNOSTICS_H
