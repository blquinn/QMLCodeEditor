#include "core/fileloader.h"
#include "core/filesaver.h"
#include "core/textdocument.h"
#include "testutil.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
#include <QtTest>

using namespace qce;
using namespace Qt::StringLiterals;

namespace {

QByteArray utf16(const QString &text, bool bigEndian, bool bom) {
  QByteArray out;
  auto put = [&](char16_t u) {
    if (bigEndian) {
      out += char(u >> 8);
      out += char(u & 0xFF);
    } else {
      out += char(u & 0xFF);
      out += char(u >> 8);
    }
  };
  if (bom)
    put(0xFEFF);
  for (QChar c : text)
    put(c.unicode());
  return out;
}

} // namespace

class TstFileIo : public QObject {
  Q_OBJECT
  QTemporaryDir m_dir;

  QString write(const QString &name, const QByteArray &bytes) {
    const QString path = m_dir.filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
      qFatal("cannot write test file");
    f.write(bytes);
    return path;
  }

private slots:
  void initTestCase() { QVERIFY(m_dir.isValid()); }

  void utf8Plain() {
    const LoadResult r = decodeBytes("héllo\nwörld\n");
    QVERIFY(r.ok);
    QCOMPARE(r.text.toString(), QStringLiteral("héllo\nwörld\n"));
    QCOMPARE(r.format.encoding, Encoding::Utf8);
    QVERIFY(!r.format.hasBom);
    QCOMPARE(r.format.dominantLineEnding, LineEnding::Lf);
    QVERIFY(!r.format.hadDecodeErrors);
  }

  void utf8Bom() {
    const LoadResult r = decodeBytes(
      "\xEF\xBB\xBF"
      "abc"
    );
    QVERIFY(r.format.hasBom);
    QCOMPARE(r.text.toString(), QStringLiteral("abc")); // the BOM is not part of the text
  }

  void utf16WithAndWithoutBom_data() {
    QTest::addColumn<bool>("bigEndian");
    QTest::addColumn<bool>("bom");
    QTest::newRow("LE bom") << false << true;
    QTest::newRow("BE bom") << true << true;
    QTest::newRow("LE no bom") << false << false;
    QTest::newRow("BE no bom") << true << false;
  }

  void utf16WithAndWithoutBom() {
    QFETCH(bool, bigEndian);
    QFETCH(bool, bom);
    const QString text = QStringLiteral("line one\r\nline two \U0001F600\r\n");
    const LoadResult r = decodeBytes(utf16(text, bigEndian, bom));
    QVERIFY(r.ok);
    QCOMPARE(r.text.toString(), text);
    QCOMPARE(r.format.encoding, bigEndian ? Encoding::Utf16BE : Encoding::Utf16LE);
    QCOMPARE(r.format.hasBom, bom);
    QCOMPARE(r.format.dominantLineEnding, LineEnding::Crlf);
  }

  void lineEndingDetection() {
    QCOMPARE(decodeBytes("a\r\nb\r\nc\n").format.dominantLineEnding, LineEnding::Crlf);
    QCOMPARE(decodeBytes("a\nb\nc\r\n").format.dominantLineEnding, LineEnding::Lf);
    QCOMPARE(decodeBytes("no newline").format.dominantLineEnding, LineEnding::Lf);
    // the text itself stays raw
    QCOMPARE(decodeBytes("a\r\nb\nc\r").text.toString(), QStringLiteral("a\r\nb\nc\r"));
  }

  void invalidUtf8IsFlagged() {
    const LoadResult r = decodeBytes(
      "ok\xFF\xFE"
      " bad\xC3"
    );
    QVERIFY(r.ok);
    QVERIFY(r.format.hadDecodeErrors);
    QVERIFY(r.text.toString().contains(QChar(0xFFFD)));
  }

  void sliceBoundariesAreHandled() {
    // A 2-byte character and a CRLF straddling the 4 MiB slice boundary.
    constexpr qsizetype slice = 4 * 1024 * 1024;
    QByteArray a(slice - 1, 'a');
    a += "\xC3"
         "\xA9"; // é split across the boundary
    a += QByteArray(slice - 2, 'b');
    a += "\r";
    a += "\n"; // CR at the end of one slice's text, LF at the start of the next? (positions shift by one)
    a += "tail\r\nx\r\n";
    const LoadResult r = decodeBytes(a);
    QVERIFY(r.ok);
    QVERIFY(!r.format.hadDecodeErrors);
    QCOMPARE(r.text.at(slice - 1), QChar(0x00E9));
    QCOMPARE(r.format.dominantLineEnding, LineEnding::Crlf);
    QCOMPARE(r.text.length(), 2 * slice + 9);
    QVERIFY(r.text.validate());
  }

  void loadsFromDisk() {
    const QString path = write("plain.txt", "one\ntwo\n");
    const LoadResult r = loadFile(path);
    QVERIFY(r.ok);
    QCOMPARE(r.text.toString(), QStringLiteral("one\ntwo\n"));
    QVERIFY(!loadFile(m_dir.filePath("missing.txt")).ok);
    QVERIFY(loadFile(m_dir.filePath("missing.txt")).error.size() > 0);
    const LoadResult empty = loadFile(write("empty.txt", ""));
    QVERIFY(empty.ok);
    QVERIFY(empty.text.isEmpty());
  }

  void documentLoadsProgressively() {
    test::Random rnd(test::testSeed());
    // ~13 MB so the loader publishes several slices
    QString text;
    while (text.size() < 6'500'000)
      text += rnd.text(50'000);
    const QByteArray bytes = text.toUtf8();
    const QString path = write("big.txt", bytes);

    TextDocument doc;
    QString mirror;
    int changes = 0;
    connect(&doc, &TextDocument::changed, this, [&](const TextChange &c) {
      ++changes;
      mirror.replace(c.start, c.removedLength(), c.inserted.toString());
    });
    int progressEvents = 0;
    qsizetype lengthAtFirstProgress = -1;
    connect(&doc, &TextDocument::loadProgress, this, [&] {
      if (progressEvents++ == 0)
        lengthAtFirstProgress = doc.length();
    });
    QSignalSpy finished(&doc, &TextDocument::loadFinished);

    doc.load(path);
    QVERIFY(doc.isLoading());
    QVERIFY(!doc.insert(0, u"x")); // refused while loading
    QVERIFY(finished.wait(30000));
    QVERIFY(!doc.isLoading());
    QVERIFY(doc.insert(0, u"x")); // accepted again
    QVERIFY(doc.remove(0, 1));

    QVERIFY2(progressEvents >= 1, "expected at least one progress event");
    QVERIFY(lengthAtFirstProgress > 0);
    QVERIFY(lengthAtFirstProgress < text.size()); // a usable prefix was visible before the end
    QVERIFY(doc.rope().toString() == text);
    QVERIFY(mirror == text); // the change events add up to the document
    QCOMPARE(doc.format().encoding, Encoding::Utf8);
    QVERIFY(doc.rope().validate());
  }

  // ---- saving ----

  void roundTripIsByteIdentical_data() {
    QTest::addColumn<QByteArray>("bytes");
    const QString text = QStringLiteral("alpha\r\nbeta\ngamma \U0001F600\r\n\r\nlast\r");
    QTest::newRow("utf8 mixed") << text.toUtf8();
    QTest::newRow("utf8 bom crlf") << QByteArray(
      "\xEF\xBB\xBF"
      "a\r\nb\r\n"
    );
    QTest::newRow("utf16le bom") << utf16(text, false, true);
    QTest::newRow("utf16be bom") << utf16(text, true, true);
    QTest::newRow("utf16le no bom") << utf16(text, false, false);
    QTest::newRow("empty") << QByteArray();
    QTest::newRow("only newline") << QByteArray("\n");
    QTest::newRow("lone cr") << QByteArray("a\rb\rc");
  }

  void roundTripIsByteIdentical() {
    QFETCH(QByteArray, bytes);
    const QString in = write("rt_in.txt", bytes);
    const LoadResult r = loadFile(in);
    QVERIFY(r.ok);
    QVERIFY(!r.format.hadDecodeErrors);
    const QString out = m_dir.filePath("rt_out.txt");
    QString error;
    QVERIFY2(saveFile(r.text, out, r.format, &error), qPrintable(error));
    QFile f(out);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QVERIFY(f.readAll() == bytes);
  }

  void roundTripOfALargeFile() {
    test::Random rnd(test::testSeed());
    QString text;
    while (text.size() < 5'000'000)
      text += rnd.text(50'000);
    const QByteArray bytes = text.toUtf8();
    const QString in = write("big_in.txt", bytes);
    const LoadResult r = loadFile(in);
    QVERIFY(r.ok);
    const QString out = m_dir.filePath("big_out.txt");
    QVERIFY(saveFile(r.text, out, r.format));
    QFile f(out);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QVERIFY(f.readAll() == bytes);
  }

  void editedDocumentSavesWithTheOriginalFormat() {
    const QString in = write("fmt.txt", utf16(u"one\r\ntwo\r\n"_s, true, true));
    TextDocument doc;
    QSignalSpy loaded(&doc, &TextDocument::loadFinished);
    doc.load(in);
    QVERIFY(loaded.wait(5000));
    doc.insert(3, u"!");
    const QString out = m_dir.filePath("fmt_out.txt");
    QVERIFY(doc.save(out));
    QFile f(out);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QVERIFY(f.readAll() == utf16(u"one!\r\ntwo\r\n"_s, true, true));
  }

  void invalidBytesAreReplacedAndFlagged() {
    const QString in = write(
      "bad.txt", QByteArray(
                   "a\xFF"
                   "b"
                 )
    );
    const LoadResult r = loadFile(in);
    QVERIFY(r.format.hadDecodeErrors);
    const QString out = m_dir.filePath("bad_out.txt");
    QVERIFY(saveFile(r.text, out, r.format));
    QFile f(out);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), QStringLiteral("a\uFFFDb").toUtf8()); // lossy, hence the flag
  }

  void saveFailureLeavesTheOriginalAlone() {
    const QString target = write("keep.txt", "original");
    QString error;
    // a path inside a directory that does not exist cannot be opened
    QVERIFY(!saveFile(Rope::fromString(u"new"), m_dir.filePath("no/such/dir/x.txt"), FileFormat(), &error));
    QVERIFY(!error.isEmpty());
    QFile f(target);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), QByteArray("original"));
  }

  void saveReplacesAnExistingFileAtomically() {
    const QString target = write("replace.txt", "old old old old");
    QVERIFY(saveFile(Rope::fromString(u"new"), target, FileFormat()));
    QFile f(target);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), QByteArray("new"));
    // no temporary files left behind
    QCOMPARE(QDir(m_dir.path()).entryList(QStringList{"replace*"}, QDir::Files).size(), 1);
  }

  void loadFailureIsReported() {
    TextDocument doc;
    QSignalSpy failed(&doc, &TextDocument::loadFailed);
    doc.load(m_dir.filePath("nope.txt"));
    QVERIFY(failed.wait(5000));
    QVERIFY(!doc.isLoading());
  }

  void cancelStopsTheLoad() {
    test::Random rnd(1);
    QString text;
    while (text.size() < 6'000'000)
      text += rnd.text(50'000);
    const QString path = write("cancel.txt", text.toUtf8());
    TextDocument doc;
    QSignalSpy finished(&doc, &TextDocument::loadFinished);
    doc.load(path);
    doc.cancelLoad();
    QVERIFY(!doc.isLoading());
    QTest::qWait(100);
    QCOMPARE(finished.count(), 0);
    QVERIFY(doc.insert(0, u"a")); // editable again
  }

  void destroyingTheDocumentMidLoadIsSafe() {
    test::Random rnd(2);
    QString text;
    while (text.size() < 6'000'000)
      text += rnd.text(50'000);
    const QString path = write("destroy.txt", text.toUtf8());
    {
      TextDocument doc;
      doc.load(path);
    }
    QTest::qWait(50);
  }
};

QTEST_MAIN(TstFileIo)
#include "tst_fileio.moc"
