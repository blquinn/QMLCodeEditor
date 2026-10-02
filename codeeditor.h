#ifndef CODEEDITOR_H
#define CODEEDITOR_H

#include <QtQuick/QQuickPaintedItem>

class CodeEditor : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_DISABLE_COPY(CodeEditor)
public:
    explicit CodeEditor(QQuickItem *parent = nullptr);
    void paint(QPainter *painter) override;
    ~CodeEditor() override;
};

#endif // CODEEDITOR_H
