#include "core/fileloader.h"

#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QStringConverter>

#include <functional>

namespace qce {

namespace {

constexpr qint64 kSliceBytes = 4 * 1024 * 1024;
constexpr qint64 kProgressIntervalMs = 50;

struct Bom {
  Encoding encoding = Encoding::Utf8;
  qsizetype length = 0;
};

Bom detectBom(QByteArrayView head) {
  if (head.size() >= 3 && uchar(head[0]) == 0xEF && uchar(head[1]) == 0xBB && uchar(head[2]) == 0xBF)
    return {Encoding::Utf8, 3};
  if (head.size() >= 2 && uchar(head[0]) == 0xFF && uchar(head[1]) == 0xFE)
    return {Encoding::Utf16LE, 2};
  if (head.size() >= 2 && uchar(head[0]) == 0xFE && uchar(head[1]) == 0xFF)
    return {Encoding::Utf16BE, 2};
  return {Encoding::Utf8, 0};
}

// Without a BOM: UTF-16 text that is mostly ASCII has a zero in every other byte.
Encoding guessWithoutBom(QByteArrayView head) {
  const qsizetype n = qMin<qsizetype>(head.size(), 64 * 1024) & ~qsizetype(1);
  if (n < 4)
    return Encoding::Utf8;
  qsizetype evenZeros = 0, oddZeros = 0;
  for (qsizetype i = 0; i < n; i += 2) {
    evenZeros += head[i] == 0;
    oddZeros += head[i + 1] == 0;
  }
  const qsizetype units = n / 2;
  if (oddZeros * 4 > units && evenZeros * 20 < units)
    return Encoding::Utf16LE;
  if (evenZeros * 4 > units && oddZeros * 20 < units)
    return Encoding::Utf16BE;
  return Encoding::Utf8;
}

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

// Feeds decoded text to the builder and counts line endings across slice boundaries.
struct LineEndingCounter {
  qint64 lf = 0;
  qint64 crlf = 0;
  bool lastWasCr = false;

  void add(QStringView text) {
    qsizetype from = 0;
    for (;;) {
      const qsizetype nl = text.indexOf(QLatin1Char('\n'), from);
      if (nl < 0)
        break;
      const bool precededByCr = nl > 0 ? text[nl - 1] == QLatin1Char('\r') : lastWasCr;
      (precededByCr ? crlf : lf)++;
      from = nl + 1;
    }
    lastWasCr = !text.isEmpty() && text.back() == QLatin1Char('\r');
  }
  LineEnding dominant() const { return crlf > lf ? LineEnding::Crlf : LineEnding::Lf; }
};

using ProgressFn = std::function<void(const Rope &, qint64 done, qint64 total)>;

enum class Outcome { Done, Canceled, Failed };

// The shared decode loop. `progress` may be empty.
Outcome decodeStream(
  QFile *file, QByteArrayView whole, RopeBuilder &builder, FileFormat *format, QString *error,
  const std::atomic<bool> *cancel, const ProgressFn &progress
) {
  // `whole` is the mapped file if mapping worked; otherwise slices are read from `file`.
  const qint64 total = file ? file->size() : whole.size();
  QByteArray readBuffer;
  auto slice = [&](qint64 at, qint64 want) -> QByteArrayView {
    if (!file || !whole.isNull())
      return whole.sliced(at, qMin(want, whole.size() - at));
    readBuffer = file->read(want);
    return readBuffer;
  };

  QByteArrayView head = slice(0, qMin<qint64>(total, 64 * 1024));
  const Bom bom = detectBom(head);
  format->hasBom = bom.length > 0;
  format->encoding = bom.length > 0 ? bom.encoding : guessWithoutBom(head);
  if (whole.isNull() && file)
    file->seek(bom.length);
  QStringDecoder decoder(qtEncoding(format->encoding));
  LineEndingCounter lines;

  QElapsedTimer sinceProgress;
  bool first = true;
  qint64 done = bom.length;
  while (done < total) {
    if (cancel && cancel->load())
      return Outcome::Canceled;
    const QByteArrayView bytes = slice(done, kSliceBytes);
    if (bytes.isEmpty()) {
      if (error)
        *error = file ? file->errorString() : QStringLiteral("short read");
      return Outcome::Failed;
    }
    const QString text = decoder(bytes);
    lines.add(text);
    builder.append(text);
    done += bytes.size();
    if (progress && (first || sinceProgress.elapsed() >= kProgressIntervalMs) && done < total) {
      progress(builder.snapshot(), done, total);
      sinceProgress.restart();
      first = false;
    }
  }
  format->dominantLineEnding = lines.dominant();
  format->hadDecodeErrors = decoder.hasError();
  return Outcome::Done;
}

LoadResult loadInternal(
  const QString &path, const std::atomic<bool> *cancel, const ProgressFn &progress, Outcome *outcome
) {
  LoadResult result;
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    result.error = file.errorString();
    *outcome = Outcome::Failed;
    return result;
  }
  QByteArrayView whole;
  if (file.size() > 0) {
    if (const uchar *mapped = file.map(0, file.size()))
      whole = QByteArrayView(reinterpret_cast<const char *>(mapped), file.size());
  }
  RopeBuilder builder;
  *outcome = decodeStream(&file, whole, builder, &result.format, &result.error, cancel, progress);
  if (*outcome == Outcome::Done) {
    result.text = builder.finish();
    result.ok = true;
  }
  return result;
}

} // namespace

LoadResult loadFile(const QString &path) {
  Outcome outcome;
  return loadInternal(path, nullptr, {}, &outcome);
}

LoadResult decodeBytes(QByteArrayView bytes) {
  LoadResult result;
  RopeBuilder builder;
  if (decodeStream(nullptr, bytes, builder, &result.format, &result.error, nullptr, {}) == Outcome::Done) {
    result.text = builder.finish();
    result.ok = true;
  }
  return result;
}

FileLoadJob::FileLoadJob(QString path, QObject *parent) : QObject(parent), m_path(std::move(path)) {
  qRegisterMetaType<qce::Rope>();
  qRegisterMetaType<qce::FileFormat>();
}

FileLoadJob::~FileLoadJob() {
  cancel();
  if (m_thread.joinable())
    m_thread.join();
}

void FileLoadJob::post(Event event) {
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_events.push_back(std::move(event));
  }
  QMetaObject::invokeMethod(this, "deliver", Qt::QueuedConnection);
}

void FileLoadJob::deliver() {
  std::deque<Event> events;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    events.swap(m_events);
  }
  for (const Event &e : events) {
    switch (e.kind) {
    case Event::Progress:
      emit progress(e.text, e.done, e.total);
      break;
    case Event::Finished:
      emit finished(e.text, e.format);
      break;
    case Event::Failed:
      emit failed(e.error);
      break;
    case Event::Canceled:
      emit canceled();
      break;
    }
  }
}

void FileLoadJob::start() {
  Q_ASSERT(!m_thread.joinable());
  // A std::thread rather than the Qt thread pool: the destructor joins it, so `this` outlives it,
  // and thread creation is visible to ThreadSanitizer as a synchronization point.
  m_thread = std::thread([this] {
    Outcome outcome;
    LoadResult result = loadInternal(
      m_path, &m_cancel,
      [this](const Rope &text, qint64 done, qint64 total) {
        Event e;
        e.kind = Event::Progress;
        e.text = text;
        e.done = done;
        e.total = total;
        post(std::move(e));
      },
      &outcome
    );
    Event e;
    switch (outcome) {
    case Outcome::Done:
      e.kind = Event::Finished;
      e.text = result.text;
      e.format = result.format;
      break;
    case Outcome::Canceled:
      e.kind = Event::Canceled;
      break;
    case Outcome::Failed:
      e.kind = Event::Failed;
      e.error = result.error;
      break;
    }
    post(std::move(e));
  });
}

} // namespace qce
