#ifndef CODEEDITOR_H
#define CODEEDITOR_H

#include <QtQuick/QQuickItem>
#include <QtQml/qqmlregistration.h>

class CodeEditor : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_DISABLE_COPY(CodeEditor)
public:
    explicit CodeEditor(QQuickItem *parent = nullptr);
    ~CodeEditor() override;
};

#endif // CODEEDITOR_H
