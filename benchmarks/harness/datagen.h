#ifndef QCE_DATAGEN_H
#define QCE_DATAGEN_H

// Deterministic synthetic source-like text for benchmarks and tests. The same (shape, seed) always yields the
// same text, on every platform (own PRNG, no std::distribution).

#include <QtCore/QString>

namespace qce::bench {

enum class Shape {
    ManyShortLines, // typical source: lines of 0-100 chars, indentation
    FewLongLines,   // lines of 10k-100k chars
    OneGiantLine,   // no line break at all (minified file)
};

enum class LineEnding { Lf, CrLf };

// Produces text incrementally so multi-hundred-MB files never sit in memory twice.
class SyntheticText
{
public:
    SyntheticText(Shape shape, quint32 seed, LineEnding lineEnding = LineEnding::Lf);

    // Next `chars` UTF-16 units of text (ASCII only, so also `chars` UTF-8 bytes). Never ends mid-way through
    // a CRLF pair.
    QString next(qint64 chars);

private:
    quint64 random();
    quint32 below(quint32 n) { return quint32(random() % n); }
    void appendLine(QString &out);

    Shape m_shape;
    LineEnding m_lineEnding;
    quint64 m_state;
    QString m_pending; // generated but not yet returned
};

// A whole in-memory text of `chars` UTF-16 units (one more if that would split a CRLF pair). It may end
// mid-line.
QString generateText(Shape shape, qint64 chars, quint32 seed = 1, LineEnding lineEnding = LineEnding::Lf);

// Writes about `bytes` of UTF-8 (ASCII) to `path`; returns an empty string on success, otherwise an error message.
QString writeSyntheticFile(const QString &path, Shape shape, qint64 bytes, quint32 seed = 1,
                           LineEnding lineEnding = LineEnding::Lf);

} // namespace qce::bench

#endif // QCE_DATAGEN_H
