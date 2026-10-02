#include "core/filesaver.h"

#include <QtCore/QByteArray>
#include <QtCore/QSaveFile>
#include <QtCore/QStringConverter>

namespace qce {

namespace {

constexpr qsizetype kFlushBytes = 1024 * 1024;

QStringConverter::Encoding qtEncoding(Encoding e) {
  switch (e) {
  case Encoding::Utf16LE:
    return QStringConverter::Utf16LE;
  case Encoding::Utf16BE:
    return QStringConverter::Utf16BE;
  case Encoding::Utf8:
    break;
  }
  return QStringConverter::Utf8;
}

QByteArray bomFor(Encoding e) {
  switch (e) {
  case Encoding::Utf16LE:
    return QByteArray("\xFF\xFE", 2);
  case Encoding::Utf16BE:
    return QByteArray("\xFE\xFF", 2);
  case Encoding::Utf8:
    break;
  }
  return QByteArray("\xEF\xBB\xBF", 3);
}

} // namespace

bool saveFile(const Rope &text, const QString &path, const FileFormat &format, QString *error) {
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    if (error)
      *error = file.errorString();
    return false;
  }

  QStringEncoder encoder(qtEncoding(format.encoding));
  QByteArray buffer;
  buffer.reserve(kFlushBytes + 4096);
  if (format.hasBom)
    buffer += bomFor(format.encoding);

  auto flush = [&] {
    if (buffer.isEmpty())
      return true;
    const bool ok = file.write(buffer) == buffer.size();
    buffer.clear();
    return ok;
  };

  ChunkIterator it(text);
  QStringView chunk;
  while (it.next(&chunk)) {
    buffer += encoder(chunk);
    if (buffer.size() >= kFlushBytes && !flush()) {
      if (error)
        *error = file.errorString();
      file.cancelWriting();
      return false;
    }
  }
  if (!flush() || !file.commit()) {
    if (error)
      *error = file.errorString();
    return false;
  }
  return true;
}

} // namespace qce
