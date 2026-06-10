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
    Q_INVOKABLE void togglePlayback();

    MediaEngine *mediaEngine() const;

signals:
    void requestOpenFile();
    void playbackStateChanged(bool isPlaying);

private:
    MediaEngine *m_mediaEngine;
};
