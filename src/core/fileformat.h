#ifndef QCE_FILEFORMAT_H
#define QCE_FILEFORMAT_H

#include <QtCore/QMetaType>

namespace qce {

enum class Encoding : quint8 { Utf8, Utf16LE, Utf16BE };
enum class LineEnding : quint8 { Lf, Crlf };

// How a file was encoded on disk, kept so that saving writes it back the same way. The text itself
// is stored raw (ADR 0007), so mixed line endings survive regardless of `dominantLineEnding`; that
// field only says what new lines should use.
struct FileFormat {
  Encoding encoding = Encoding::Utf8;
  bool hasBom = false;
  LineEnding dominantLineEnding = LineEnding::Lf;
  // Invalid bytes were replaced by U+FFFD while loading: saving would not reproduce the file, so a
  // host should warn before overwriting it.
  bool hadDecodeErrors = false;

  friend constexpr bool operator==(const FileFormat &, const FileFormat &) = default;
};

} // namespace qce

Q_DECLARE_METATYPE(qce::FileFormat)

#endif // QCE_FILEFORMAT_H
