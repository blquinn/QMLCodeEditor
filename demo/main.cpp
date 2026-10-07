#include <QtCore/QDir>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlApplicationEngine>
#include <QtQuick/QQuickWindow>
#include <QtQuickControls2/QQuickStyle>

int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  // The demo's look is fixed (see the palette in Main.qml) rather than following the platform style.
  QQuickStyle::setStyle(QStringLiteral("Basic"));
  // --smoke: exit 0 after the first presented frame, 1 if none arrives within 10 s (used by ctest).
  // --grab <png>: save a screenshot of the window once it has settled, then exit.
  // --fold-level <n>: fold the regions of that nesting level shortly before the grab.
  // --occurrences: select every occurrence of the word at the start of the text before the grab.
  // --diagnostics <n>: show n sample diagnostics (and with --hints some inlay hints) before the grab.
  // --vim: start in vim mode; --vim-keys <keys>: then run keys (vim notation) before the grab.
  bool smoke = false;
  int foldLevel = 0;
  int diagnostics = 0;
  bool hints = false;
  bool occurrences = false;
  bool vim = false;
  QString vimKeys;
  QString grabPath;
  QUrl initialFile; // the first argument that isn't an option is a file to open
  const QStringList args = app.arguments();
  for (int i = 1; i < args.size(); ++i) {
    if (args[i] == QLatin1String("--smoke"))
      smoke = true;
    else if (args[i] == QLatin1String("--grab") && i + 1 < args.size())
      grabPath = args[++i];
    else if (args[i] == QLatin1String("--occurrences"))
      occurrences = true;
    else if (args[i] == QLatin1String("--fold-level") && i + 1 < args.size())
      foldLevel = args[++i].toInt();
    else if (args[i] == QLatin1String("--diagnostics") && i + 1 < args.size())
      diagnostics = args[++i].toInt();
    else if (args[i] == QLatin1String("--hints"))
      hints = true;
    else if (args[i] == QLatin1String("--vim"))
      vim = true;
    else if (args[i] == QLatin1String("--vim-keys") && i + 1 < args.size()) {
      vim = true;
      vimKeys = args[++i];
    }
    else if (!args[i].startsWith(QLatin1Char('-')) && initialFile.isEmpty())
      initialFile = QUrl::fromLocalFile(QDir::current().absoluteFilePath(args[i]));
  }

  QQmlApplicationEngine engine;
  engine.setInitialProperties({{QStringLiteral("initialFile"), initialFile}});
  int exitCode = 0;
  QObject::connect(
    &engine, &QQmlApplicationEngine::objectCreationFailed, &app, [&] { QCoreApplication::exit(1); },
    Qt::QueuedConnection
  );
  engine.loadFromModule("Demo", "Main");

  const auto windows = engine.rootObjects();
  auto *window = windows.isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(windows.first());
  if ((smoke || !grabPath.isEmpty()) && !window) {
    qWarning("demo: Main.qml did not create a window");
    return 1;
  }
  if (smoke) {
    QObject::connect(
      window, &QQuickWindow::frameSwapped, &app, [] { QCoreApplication::exit(0); }, Qt::QueuedConnection
    );
    QTimer::singleShot(10'000, &app, [&] {
      qWarning("demo --smoke: no frame within 10 s");
      exitCode = 1;
      QCoreApplication::exit(1);
    });
  }
  if (foldLevel > 0) {
    QTimer::singleShot(1000, &app, [&] {
      if (QObject *editor = window->findChild<QObject *>(QStringLiteral("editor")))
        QMetaObject::invokeMethod(editor, "foldToLevel", Q_ARG(int, foldLevel));
    });
  }
  if (diagnostics > 0 || hints) {
    QTimer::singleShot(1000, &app, [&] {
      if (diagnostics > 0)
        QMetaObject::invokeMethod(window, "sampleDiagnostics", Q_ARG(QVariant, diagnostics));
      if (hints)
        QMetaObject::invokeMethod(window, "sampleHints");
    });
  }
  if (vim) {
    QTimer::singleShot(600, &app, [&] {
      if (QObject *editor = window->findChild<QObject *>(QStringLiteral("editor"))) {
        editor->setProperty("vimMode", true);
        if (!vimKeys.isEmpty())
          QMetaObject::invokeMethod(editor, "sendVimKeys", Q_ARG(QString, vimKeys));
      }
    });
  }
  if (occurrences) {
    QTimer::singleShot(1000, &app, [&] {
      if (QObject *editor = window->findChild<QObject *>(QStringLiteral("editor"))) {
        editor->setProperty("cursorPosition", 1);
        QMetaObject::invokeMethod(editor, "selectAllOccurrences");
      }
    });
  }
  if (!grabPath.isEmpty()) {
    QTimer::singleShot(1500, &app, [&] {
      exitCode = window->grabWindow().save(grabPath) ? 0 : 1;
      QCoreApplication::exit(exitCode);
    });
  }
  const int rc = app.exec();
  return rc ? rc : exitCode;
}
