#pragma once

#include <QObject>
#include <QString>

class MediaEngine;

class ApplicationController : public QObject
{
    Q_OBJECT

public:
    explicit ApplicationController(QObject *parent = nullptr);

    Q_INVOKABLE bool openFile();
    Q_INVOKABLE bool loadFile(const QString &path);

    MediaEngine *mediaEngine() const;

signals:
    void requestOpenFile();

private:
    MediaEngine *m_mediaEngine;
};
