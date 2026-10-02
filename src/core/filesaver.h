#ifndef QCE_FILESAVER_H
#define QCE_FILESAVER_H

#include "core/fileformat.h"
#include "core/rope.h"

#include <QtCore/QString>

namespace qce {

// Writes `text` to `path` in `format`, atomically: the data goes to a temporary file that replaces
// the target only when everything was written (QSaveFile), keeping the target's permissions. The
// BOM is written iff format.hasBom. Line endings are whatever the text contains (ADR 0007), so a
// file loaded and saved unchanged is byte-identical unless it contained invalid bytes
// (FileFormat::hadDecodeErrors). Safe to call from a worker thread with a snapshot's rope.
// Returns false and fills `error` on failure.
bool saveFile(const Rope &text, const QString &path, const FileFormat &format, QString *error = nullptr);

} // namespace qce

#endif // QCE_FILESAVER_H
