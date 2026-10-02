#include "datagen.h"

#include <QtCore/QSaveFile>

#include <algorithm>
#include <array>

namespace qce::bench {

namespace {

constexpr std::array<const char *, 40> kWords = {
    "int",   "const", "return", "if",     "else",    "for",    "while", "auto",   "void",   "class",
    "struct", "value", "index",  "count",  "buffer",  "node",   "child", "parent", "offset", "length",
    "size",  "begin", "end",    "insert", "remove",  "update", "text",  "line",   "column", "cursor",
    "Item",  "QString", "std",  "ptr",    "result",  "error",  "name",  "type",   "data",   "state"};
constexpr std::array<const char *, 12> kPunct = {"(", ")", " = ", ", ", "; ", " + ", "::", "->", "[", "]", " < ", "."};

} // namespace

SyntheticText::SyntheticText(Shape shape, quint32 seed, LineEnding lineEnding)
    : m_shape(shape), m_lineEnding(lineEnding), m_state(0x9E3779B97F4A7C15ull ^ (quint64(seed) * 0xBF58476D1CE4E5B9ull))
{
}

// splitmix64
quint64 SyntheticText::random()
{
    quint64 z = (m_state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

void SyntheticText::appendLine(QString &out)
{
    qint64 target = 0;
    switch (m_shape) {
    case Shape::ManyShortLines:
        // ~10% blank lines, otherwise 10-100 chars with 0-6 levels of indentation
        target = below(10) == 0 ? 0 : 10 + below(91);
        out.append(QString(int(below(7)) * 4, QLatin1Char(' ')));
        break;
    case Shape::FewLongLines:
        target = 10'000 + below(90'001);
        break;
    case Shape::OneGiantLine:
        target = 1 << 20; // generated in 1M-char pieces, no line break
        break;
    }
    const qsizetype start = out.size();
    while (out.size() - start < target) {
        out.append(QLatin1String(kWords[below(kWords.size())]));
        out.append(QLatin1String(kPunct[below(kPunct.size())]));
        if (below(16) == 0)
            out.append(QLatin1Char('\t'));
    }
    if (m_shape != Shape::OneGiantLine)
        out.append(m_lineEnding == LineEnding::CrLf ? QStringLiteral("\r\n") : QStringLiteral("\n"));
}

QString SyntheticText::next(qint64 chars)
{
    while (m_pending.size() < chars + 1) // +1 so we can avoid splitting a CRLF pair
        appendLine(m_pending);
    qint64 take = chars;
    if (take > 0 && m_pending.at(take - 1) == QLatin1Char('\r'))
        ++take;
    QString out = m_pending.left(take);
    m_pending.remove(0, take);
    return out;
}

QString generateText(Shape shape, qint64 chars, quint32 seed, LineEnding lineEnding)
{
    SyntheticText gen(shape, seed, lineEnding);
    return gen.next(chars);
}

QString writeSyntheticFile(const QString &path, Shape shape, qint64 bytes, quint32 seed, LineEnding lineEnding)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return file.errorString();
    SyntheticText gen(shape, seed, lineEnding);
    constexpr qint64 kSlice = 4 << 20;
    for (qint64 written = 0; written < bytes;) {
        const QByteArray chunk = gen.next(std::min(kSlice, bytes - written)).toLatin1();
        if (file.write(chunk) != chunk.size())
            return file.errorString();
        written += chunk.size();
    }
    return file.commit() ? QString() : file.errorString();
}

} // namespace qce::bench
