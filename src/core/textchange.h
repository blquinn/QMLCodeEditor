#ifndef QCE_TEXTCHANGE_H
#define QCE_TEXTCHANGE_H

#include "core/rope.h"
#include "core/textposition.h"

namespace qce {

// One applied edit, shaped to feed tree-sitter's TSInputEdit and LSP's
// TextDocumentContentChangeEvent without translation:
//  - tree-sitter (UTF-16): start_byte = start * 2, old_end_byte = oldEnd * 2, new_end_byte = newEnd * 2;
//    TSPoint = {row = line, column = column * 2}.
//  - LSP: range = [startPos, oldEndPos) in the text before the edit, text = inserted.toString().
// start/oldEnd/newEnd are UTF-16 offsets. oldEndPos is computed on the text before the edit;
// startPos and newEndPos are valid on the text after it (startPos is identical in both).
struct TextChange {
  qsizetype start = 0;
  qsizetype oldEnd = 0;
  qsizetype newEnd = 0;
  TextPosition startPos;
  TextPosition oldEndPos;
  TextPosition newEndPos;
  Rope removed;  // text that was in [start, oldEnd)
  Rope inserted; // text now in [start, newEnd)
  quint64 versionBefore = 0;
  quint64 versionAfter = 0;

  qsizetype removedLength() const { return oldEnd - start; }
  qsizetype insertedLength() const { return newEnd - start; }
};

} // namespace qce

#endif // QCE_TEXTCHANGE_H
