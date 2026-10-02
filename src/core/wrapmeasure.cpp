#include "core/wrapmeasure.h"

#include <QtCore/QChar>

namespace qce {

bool isClusterExtender(char32_t cp) {
  if (cp < 0x300)
    return false;
  if (cp == 0x200D || (cp >= 0x1F3FB && cp <= 0x1F3FF))
    return true;
  switch (QChar::category(cp)) {
  case QChar::Mark_NonSpacing:
  case QChar::Mark_SpacingCombining:
  case QChar::Mark_Enclosing:
    return true;
  default:
    return false;
  }
}

namespace {

bool isWide(char32_t cp) {
  return (cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0xA4CF) || (cp >= 0xAC00 && cp <= 0xD7A3) ||
         (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFE30 && cp <= 0xFE6F) || (cp >= 0xFF00 && cp <= 0xFF60) ||
         (cp >= 0xFFE0 && cp <= 0xFFE6) || (cp >= 0x1F300 && cp <= 0x1F64F) ||
         (cp >= 0x1F900 && cp <= 0x1F9FF) || (cp >= 0x20000 && cp <= 0x3FFFD);
}

} // namespace

qreal GridWrapMeasure::advance(char32_t cp) const {
  if (cp < 0x300)
    return m_cell;
  if (isClusterExtender(cp))
    return 0;
  return isWide(cp) ? 2 * m_cell : m_cell;
}

} // namespace qce
