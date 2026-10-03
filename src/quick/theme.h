#ifndef QCE_THEME_H
#define QCE_THEME_H

#include "core/highlighter.h"

#include <QtCore/QObject>
#include <QtCore/QVariantMap>
#include <QtGui/QColor>
#include <QtGui/QTextCharFormat>
#include <QtGui/QTextLayout>
#include <QtQml/qqmlregistration.h>

namespace qce {

// Colors and token styles for the editor. Swapping a theme (or assigning to one of its properties)
// invalidates the editor's layouts and repaints it, so light/dark switching is one assignment.
class Theme : public QObject {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(QColor background MEMBER m_background NOTIFY changed)
  Q_PROPERTY(QColor foreground MEMBER m_foreground NOTIFY changed)
  Q_PROPERTY(QColor selection MEMBER m_selection NOTIFY changed)
  Q_PROPERTY(QColor selectionForeground MEMBER m_selectionForeground NOTIFY changed)
  Q_PROPERTY(QColor cursor MEMBER m_cursor NOTIFY changed)
  Q_PROPERTY(QColor currentLine MEMBER m_currentLine NOTIFY changed)
  Q_PROPERTY(QColor whitespace MEMBER m_whitespace NOTIFY changed)
  Q_PROPERTY(QColor gutterBackground MEMBER m_gutterBackground NOTIFY changed)
  Q_PROPERTY(QColor lineNumber MEMBER m_lineNumber NOTIFY changed)
  Q_PROPERTY(QColor currentLineNumber MEMBER m_currentLineNumber NOTIFY changed)
  Q_PROPERTY(QColor changeModified MEMBER m_changeModified NOTIFY changed)
  Q_PROPERTY(QColor changeDeleted MEMBER m_changeDeleted NOTIFY changed)
  Q_PROPERTY(QColor foldMarker MEMBER m_foldMarker NOTIFY changed)
  Q_PROPERTY(QColor foldMarkerHover MEMBER m_foldMarkerHover NOTIFY changed)
  Q_PROPERTY(QColor foldRangeHover MEMBER m_foldRangeHover NOTIFY changed)
  Q_PROPERTY(QColor foldPlaceholder MEMBER m_foldPlaceholder NOTIFY changed)
  Q_PROPERTY(QColor foldPlaceholderText MEMBER m_foldPlaceholderText NOTIFY changed)
  // Style name -> { color, bold, italic }; names are the TokenStyle names from core/highlighter.h
  // in lower case ("keyword", "string", ...). Unlisted styles use the foreground color.
  Q_PROPERTY(QVariantMap tokenStyles READ tokenStyles WRITE setTokenStyles NOTIFY changed)
public:
  explicit Theme(QObject *parent = nullptr);

  static Theme *createDark(QObject *parent = nullptr);
  static Theme *createLight(QObject *parent = nullptr);

  QColor background() const { return m_background; }
  QColor foreground() const { return m_foreground; }
  QColor selection() const { return m_selection; }
  QColor selectionForeground() const { return m_selectionForeground; }
  QColor cursor() const { return m_cursor; }
  QColor currentLine() const { return m_currentLine; }
  QColor whitespace() const { return m_whitespace; }
  QColor gutterBackground() const { return m_gutterBackground; }
  QColor lineNumber() const { return m_lineNumber; }
  QColor currentLineNumber() const { return m_currentLineNumber; }
  QColor changeModified() const { return m_changeModified; }
  QColor changeDeleted() const { return m_changeDeleted; }
  QColor foldMarker() const { return m_foldMarker; }
  QColor foldMarkerHover() const { return m_foldMarkerHover; }
  QColor foldRangeHover() const { return m_foldRangeHover; }
  QColor foldPlaceholder() const { return m_foldPlaceholder; }
  QColor foldPlaceholderText() const { return m_foldPlaceholderText; }

  QVariantMap tokenStyles() const { return m_tokenStyles; }
  void setTokenStyles(const QVariantMap &styles);

  // Character format for a token style; the default style (and any style the theme doesn't list)
  // is the foreground color.
  QTextCharFormat charFormat(qce::TokenStyle style) const;
  // Layout format ranges for spans of one line; Default spans are skipped.
  QList<QTextLayout::FormatRange> formatRanges(const QList<qce::HighlightSpan> &spans) const;

  // Loads the built-in "dark" or "light" palette and token styles (one changed() signal).
  Q_INVOKABLE void applyPreset(const QString &name);

  // Copies every value from `other` (one changed() signal).
  void assign(const Theme &other);

signals:
  void changed();

private:
  QColor m_background{0x1e, 0x1e, 0x1e};
  QColor m_foreground{0xd4, 0xd4, 0xd4};
  QColor m_selection{0x26, 0x4f, 0x78};
  QColor m_selectionForeground; // invalid = keep the text color
  QColor m_cursor{0xae, 0xaf, 0xad};
  QColor m_currentLine{0x2a, 0x2d, 0x2e};
  QColor m_whitespace{0x40, 0x40, 0x40};
  QColor m_gutterBackground{0x1e, 0x1e, 0x1e};
  QColor m_lineNumber{0x85, 0x85, 0x85};
  QColor m_currentLineNumber{0xc6, 0xc6, 0xc6};
  QColor m_changeModified{0x1b, 0x81, 0xa8};
  QColor m_changeDeleted{0xf1, 0x4c, 0x4c};
  QColor m_foldMarker{0x85, 0x85, 0x85};         // chevrons in the fold column
  QColor m_foldMarkerHover{0xc6, 0xc6, 0xc6};    // the chevron under the pointer
  QColor m_foldRangeHover{0xff, 0xff, 0xff, 0x12}; // band over the hovered fold's rows
  QColor m_foldPlaceholder{0x4b, 0x4b, 0x4b};    // the chip after a folded line
  QColor m_foldPlaceholderText{0xd4, 0xd4, 0xd4}; // its dots
  void rebuildFormats();

  QVariantMap m_tokenStyles;
  QTextCharFormat m_formats[size_t(qce::TokenStyle::Count)];
};

} // namespace qce

#endif // QCE_THEME_H
