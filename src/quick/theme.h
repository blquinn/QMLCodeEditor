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
  Q_PROPERTY(QColor diagnosticError MEMBER m_diagnosticError NOTIFY changed)
  Q_PROPERTY(QColor diagnosticWarning MEMBER m_diagnosticWarning NOTIFY changed)
  Q_PROPERTY(QColor diagnosticInfo MEMBER m_diagnosticInfo NOTIFY changed)
  Q_PROPERTY(QColor diagnosticHint MEMBER m_diagnosticHint NOTIFY changed)
  // End-of-line virtual text, and inlay hints with the pill behind them.
  Q_PROPERTY(QColor virtualText MEMBER m_virtualText NOTIFY changed)
  Q_PROPERTY(QColor inlayHint MEMBER m_inlayHint NOTIFY changed)
  Q_PROPERTY(QColor inlayHintBackground MEMBER m_inlayHintBackground NOTIFY changed)
  Q_PROPERTY(QColor bracketMatch MEMBER m_bracketMatch NOTIFY changed)
  Q_PROPERTY(QColor searchMatch MEMBER m_searchMatch NOTIFY changed)
  Q_PROPERTY(QColor indentGuide MEMBER m_indentGuide NOTIFY changed)
  Q_PROPERTY(QColor indentGuideActive MEMBER m_indentGuideActive NOTIFY changed)
  // Style name -> { color, background, bold, italic }; names are the TokenStyle names from core/highlighter.h
  // in lower case ("keyword", "string", ...) or the names of styles a host registered (registerTokenStyle).
  // Unlisted styles use the foreground color and no background. A background is drawn behind the text.
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
  QColor diagnosticError() const { return m_diagnosticError; }
  QColor diagnosticWarning() const { return m_diagnosticWarning; }
  QColor diagnosticInfo() const { return m_diagnosticInfo; }
  QColor diagnosticHint() const { return m_diagnosticHint; }
  QColor virtualText() const { return m_virtualText; }
  QColor inlayHint() const { return m_inlayHint; }
  QColor inlayHintBackground() const { return m_inlayHintBackground; }
  QColor bracketMatch() const { return m_bracketMatch; }
  QColor searchMatch() const { return m_searchMatch; }
  QColor indentGuide() const { return m_indentGuide; }
  QColor indentGuideActive() const { return m_indentGuideActive; }
  // The color for a decoration severity (qce::DecorationSeverity, LSP numbering); the foreground for none.
  Q_INVOKABLE QColor severityColor(int severity) const;

  QVariantMap tokenStyles() const { return m_tokenStyles; }
  void setTokenStyles(const QVariantMap &styles);

  // Character format for a token style; the default style (and any style the theme doesn't list)
  // is the foreground color.
  QTextCharFormat charFormat(qce::TokenStyle style) const;
  // Layout format ranges for spans of one line; Default spans are skipped.
  QList<QTextLayout::FormatRange> formatRanges(const QList<qce::HighlightSpan> &spans) const;
  // The background of a token style (invalid when it has none). hasStyleBackgrounds() is false when no
  // style has one, so layouts can skip looking.
  QColor styleBackground(qce::TokenStyle style) const;
  bool hasStyleBackgrounds() const { return m_hasBackgrounds; }

  // Defines (and registers, if new) a token style by name in one step; emits changed() once. An invalid
  // `background` means none. Does nothing if the name cannot be registered (empty, or all slots taken).
  Q_INVOKABLE void setTokenStyle(
    const QString &name, const QColor &color, const QColor &background = QColor(), bool bold = false,
    bool italic = false
  );

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
  QColor m_diagnosticError{0xf1, 0x4c, 0x4c};
  QColor m_diagnosticWarning{0xcc, 0xa7, 0x00};
  QColor m_diagnosticInfo{0x37, 0x94, 0xff};
  QColor m_diagnosticHint{0x9a, 0x9a, 0x9a};
  QColor m_virtualText{0x7a, 0x7a, 0x7a};
  QColor m_inlayHint{0x8b, 0x94, 0x9f};
  QColor m_inlayHintBackground{0x40, 0x44, 0x4a, 0x80};
  QColor m_bracketMatch{0x4a, 0x50, 0x58, 0xa0};
  QColor m_searchMatch{0xe5, 0xc0, 0x7b, 0x55};
  QColor m_indentGuide{0x3a, 0x3e, 0x44};
  QColor m_indentGuideActive{0x6a, 0x70, 0x7a};
  void rebuildFormats();
  void buildFormat(size_t index, const QString &name) const;

  QVariantMap m_tokenStyles;
  // Indexed by the style's value: built-ins first, host-registered ones from TokenStyle::FirstCustom.
  // Styles registered after the last rebuild get theirs on first use.
  mutable QTextCharFormat m_formats[256];
  mutable QColor m_backgrounds[256];
  mutable int m_customBuilt = 0; // how many registered custom styles m_formats covers
  bool m_hasBackgrounds = false;
};

} // namespace qce

#endif // QCE_THEME_H
