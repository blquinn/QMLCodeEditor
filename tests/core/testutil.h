#ifndef QCE_TESTUTIL_H
#define QCE_TESTUTIL_H

#include <QtCore/QString>
#include <QtCore/QtGlobal>

#include <random>

namespace qce::test {

// Seed and iteration count come from the environment so long soaks can be run on demand.
inline quint32 testSeed(quint32 fallback = 1) {
  bool ok = false;
  const quint32 v = qEnvironmentVariable("QCE_TEST_SEED").toUInt(&ok);
  return ok ? v : fallback;
}

inline int testIterations(int fallback) {
  bool ok = false;
  const int v = qEnvironmentVariable("QCE_TEST_ITERATIONS").toInt(&ok);
  return ok && v > 0 ? v : fallback;
}

// Random text over an alphabet that stresses the text core: newlines, CR, CRLF, surrogate pairs,
// combining marks and plain letters.
class Random {
public:
  explicit Random(quint32 seed) : m_rng(seed) {}

  int below(int n) { return int(m_rng() % quint32(n)); }
  int range(int lo, int hi) { return lo + below(hi - lo + 1); }
  bool chance(int percent) { return below(100) < percent; }

  QString text(int units) {
    static const QString pieces[] = {
      QStringLiteral("a"),  QStringLiteral("b"),    QStringLiteral("c"),  QStringLiteral(" "),
      QStringLiteral("\n"), QStringLiteral("\r\n"), QStringLiteral("\r"), QStringLiteral("\U0001F600"),
      QStringLiteral("é"),  QStringLiteral("é"),    QStringLiteral("_"),  QStringLiteral("("),
    };
    QString out;
    while (out.size() < units)
      out += pieces[below(int(std::size(pieces)))];
    out.truncate(units);
    if (!out.isEmpty() && out.back().isHighSurrogate())
      out.chop(1); // never end in half a pair, so the text survives a UTF-8 round trip
    return out;
  }

private:
  std::mt19937 m_rng;
};

} // namespace qce::test

#endif // QCE_TESTUTIL_H
