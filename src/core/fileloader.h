#ifndef QCE_FILELOADER_H
#define QCE_FILELOADER_H

#include "core/fileformat.h"
#include "core/rope.h"

#include <QtCore/QObject>
#include <QtCore/QString>

#include <atomic>
#include <deque>
#include <mutex>
#include <thread>

namespace qce {

struct LoadResult {
  bool ok = false;
  Rope text;
  FileFormat format;
  QString error;
};

// Decodes a whole file (or byte buffer) on the calling thread. Encoding comes from the BOM, else a
// UTF-16 heuristic, else UTF-8; invalid bytes become U+FFFD and set FileFormat::hadDecodeErrors.
LoadResult loadFile(const QString &path);
LoadResult decodeBytes(QByteArrayView bytes);

// Loads a file on a worker thread, publishing growing snapshots so the first lines can be shown
// before the rest is read: progress() fires after the first slice and then at most every
// 50 ms; finished() or failed() or canceled() ends the job. Signals arrive on the thread the job
// lives in. Destroying the job cancels it and waits for the worker.
class FileLoadJob : public QObject {
  Q_OBJECT
public:
  explicit FileLoadJob(QString path, QObject *parent = nullptr);
  ~FileLoadJob() override;

  void start();
  void cancel() { m_cancel = true; }

signals:
  // Everything decoded so far; each snapshot extends the previous one.
  void progress(const qce::Rope &text, qint64 bytesDone, qint64 bytesTotal);
  void finished(const qce::Rope &text, const qce::FileFormat &format);
  void failed(const QString &error);
  void canceled();

private slots:
  void deliver();

private:
  // Worker -> job hand-off: events wait in a mutex-protected queue and one argument-less queued
  // call wakes the job's thread, so no payload travels through Qt's event machinery.
  struct Event {
    enum Kind { Progress, Finished, Failed, Canceled } kind = Progress;
    Rope text;
    FileFormat format;
    qint64 done = 0;
    qint64 total = 0;
    QString error;
  };
  void post(Event event);

  QString m_path;
  std::mutex m_mutex;
  std::deque<Event> m_events;
  std::atomic<bool> m_cancel{false};
  std::thread m_thread;
};

} // namespace qce

Q_DECLARE_METATYPE(qce::Rope)

#endif // QCE_FILELOADER_H
