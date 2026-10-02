#include <QtGui/QGuiApplication>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>
#include <QtQml/qqmlextensionplugin.h>
#include <QtQuick/QQuickItem>
#include <QtTest>

Q_IMPORT_QML_PLUGIN(me_blq_qmlcodeeditorPlugin)

class TstQuickSmoke : public QObject
{
    Q_OBJECT
private slots:
    void moduleLoads()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import me.blq.qmlcodeeditor\nCodeEditor {}\n", QUrl());
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> object(component.create());
        QVERIFY(object);
        QVERIFY(qobject_cast<QQuickItem *>(object.get()));
    }
};

QTEST_MAIN(TstQuickSmoke)
#include "tst_quick_smoke.moc"
