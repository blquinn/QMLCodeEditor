// qce_datagen: write deterministic synthetic text files for benchmarks.
//   qce_datagen --shape short --size 100M --seed 1 out.txt

#include "datagen.h"

#include <QtCore/QCommandLineParser>
#include <QtCore/QCoreApplication>

#include <cstdio>

using namespace qce::bench;

static bool parseSize(const QString &text, qint64 &out)
{
    QString t = text.trimmed().toUpper();
    qint64 mul = 1;
    if (t.endsWith(QLatin1Char('K')))
        mul = 1 << 10;
    else if (t.endsWith(QLatin1Char('M')))
        mul = 1 << 20;
    else if (t.endsWith(QLatin1Char('G')))
        mul = 1 << 30;
    if (mul != 1)
        t.chop(1);
    bool ok = false;
    const qint64 n = t.toLongLong(&ok);
    out = n * mul;
    return ok && n > 0;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Generate deterministic synthetic text files."));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("shape"), QStringLiteral("short (many short lines), long (few long lines), giant (one line)."), QStringLiteral("shape"), QStringLiteral("short")});
    parser.addOption({QStringLiteral("size"), QStringLiteral("Size, e.g. 100M, 512K, 1G."), QStringLiteral("size"), QStringLiteral("1M")});
    parser.addOption({QStringLiteral("seed"), QStringLiteral("Random seed."), QStringLiteral("n"), QStringLiteral("1")});
    parser.addOption({QStringLiteral("crlf"), QStringLiteral("Use CRLF line endings.")});
    parser.addPositionalArgument(QStringLiteral("output"), QStringLiteral("File to write."));
    parser.process(app);

    if (parser.positionalArguments().size() != 1)
        parser.showHelp(2);

    Shape shape;
    const QString shapeName = parser.value(QStringLiteral("shape"));
    if (shapeName == QLatin1String("short"))
        shape = Shape::ManyShortLines;
    else if (shapeName == QLatin1String("long"))
        shape = Shape::FewLongLines;
    else if (shapeName == QLatin1String("giant"))
        shape = Shape::OneGiantLine;
    else {
        std::fprintf(stderr, "unknown shape: %s\n", qPrintable(shapeName));
        return 2;
    }
    qint64 size = 0;
    if (!parseSize(parser.value(QStringLiteral("size")), size)) {
        std::fprintf(stderr, "bad size: %s\n", qPrintable(parser.value(QStringLiteral("size"))));
        return 2;
    }

    const QString error = writeSyntheticFile(
        parser.positionalArguments().first(), shape, size, parser.value(QStringLiteral("seed")).toUInt(),
        parser.isSet(QStringLiteral("crlf")) ? LineEnding::CrLf : LineEnding::Lf);
    if (!error.isEmpty()) {
        std::fprintf(stderr, "error: %s\n", qPrintable(error));
        return 1;
    }
    return 0;
}
