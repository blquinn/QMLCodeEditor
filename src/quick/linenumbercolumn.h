#ifndef QCE_LINENUMBERCOLUMN_H
#define QCE_LINENUMBERCOLUMN_H

#include "quick/gutter.h"

#include <QtGui/QFont>

#include <unordered_map>

namespace qce {

// Line numbers (GUTTER-02, GUTTER-03). Continuation rows of a wrapped line show nothing. The width
// follows the digit count of the last line, never the scroll position, so it only changes when the
// document gains or loses a power of ten in lines.
class LineNumberColumn : public GutterColumn {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(NumberMode mode READ mode WRITE setMode NOTIFY modeChanged FINAL)
  // The narrowest the column gets, in digits.
  Q_PROPERTY(int minimumDigits READ minimumDigits WRITE setMinimumDigits NOTIFY minimumDigitsChanged FINAL)
public:
  enum NumberMode {
    Absolute, // 1, 2, 3 ...
    Relative, // distance from the cursor line, 0 on it
    Hybrid    // distance from the cursor line, the absolute number on it
  };
  Q_ENUM(NumberMode)

  explicit LineNumberColumn(QObject *parent = nullptr);

  NumberMode mode() const { return m_mode; }
  void setMode(NumberMode mode);
  int minimumDigits() const { return m_minimumDigits; }
  void setMinimumDigits(int digits);

  qreal autoWidth(const GutterContext &context) const override;
  void paintRows(const GutterContext &context, const QList<FramePlanRow> &rows, GutterPainter &painter) override;

  // The number shown for `line` with the cursor on `cursorLine`.
  qsizetype numberFor(qsizetype line, qsizetype cursorLine) const;

signals:
  void modeChanged();
  void minimumDigitsChanged();

private:
  std::shared_ptr<LineLayout> labelFor(qsizetype number, const QFont &font);

  NumberMode m_mode = Absolute;
  int m_minimumDigits = 2;
  // Labels by number: in relative mode the same few dozen numbers recur, so moving the cursor lays
  // nothing out. Dropped when the font changes or the cache grows large.
  std::unordered_map<qsizetype, std::shared_ptr<LineLayout>> m_labels;
  QFont m_labelFont;
};

} // namespace qce

#endif // QCE_LINENUMBERCOLUMN_H
