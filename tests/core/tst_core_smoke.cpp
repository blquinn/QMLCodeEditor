#include "core/qcecore.h"

#include <QtTest>

class TstCoreSmoke : public QObject
{
    Q_OBJECT
private slots:
    void versionIsSet() { QVERIFY(!qce::coreVersion().isEmpty()); }
};

QTEST_APPLESS_MAIN(TstCoreSmoke)
#include "tst_core_smoke.moc"
