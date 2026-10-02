#ifndef QCE_WRAPMEASURE_H
#define QCE_WRAPMEASURE_H

#include <QtCore/QtGlobal>

namespace qce {

// How wide text is, for deciding where soft wrap breaks a line (ADR 0011). `core` has no fonts, so
// the host supplies this. Calls may come from a worker thread while the GUI thread also uses the
// object, so implementations must be thread-safe.
class WrapMeasure {
public:
  virtual ~WrapMeasure() = default;
  // Width of one monospace cell in pixels.
  virtual qreal cellAdvance() const = 0;
  virtual int tabWidth() const = 0; // in cells
  // Advance of a code point that is not a tab (tabs depend on where they start). Zero for
  // combining marks.
  virtual qreal advance(char32_t codePoint) const = 0;
};

// True for code points that attach to the preceding character instead of starting a cluster:
// combining marks, variation selectors, joiners and emoji skin-tone modifiers.
bool isClusterExtender(char32_t codePoint);

// One cell per code point, two for East Asian wide characters, none for combining marks: exact for
// a monospace font. Used by tests and as the fallback for hosts without font metrics.
class GridWrapMeasure : public WrapMeasure {
public:
  explicit GridWrapMeasure(int tabWidth = 4, qreal cell = 1.0) : m_tabWidth(tabWidth), m_cell(cell) {}
  qreal cellAdvance() const override { return m_cell; }
  int tabWidth() const override { return m_tabWidth; }
  qreal advance(char32_t codePoint) const override;

private:
  int m_tabWidth;
  qreal m_cell;
};

} // namespace qce

#endif // QCE_WRAPMEASURE_H
