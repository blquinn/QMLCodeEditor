#include <QtCore/QTimer>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlApplicationEngine>
#include <QtQuick/QQuickWindow>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    // --smoke: exit 0 after the first presented frame, 1 if none arrives within 10 s (used by ctest).
    const bool smoke = app.arguments().contains(QStringLiteral("--smoke"));

    QQmlApplicationEngine engine;
    int exitCode = 0;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app, [&] { QCoreApplication::exit(1); },
        Qt::QueuedConnection);
    engine.loadFromModule("Demo", "Main");

    if (smoke) {
        const auto windows = engine.rootObjects();
        auto *window = windows.isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(windows.first());
        if (!window) {
            qWarning("demo: Main.qml did not create a window");
            return 1;
        }
        QObject::connect(window, &QQuickWindow::frameSwapped, &app, [] { QCoreApplication::exit(0); },
                         Qt::QueuedConnection);
        QTimer::singleShot(10'000, &app, [&] {
            qWarning("demo --smoke: no frame within 10 s");
            exitCode = 1;
            QCoreApplication::exit(1);
        });
    }
    const int rc = app.exec();
    return rc ? rc : exitCode;
}
