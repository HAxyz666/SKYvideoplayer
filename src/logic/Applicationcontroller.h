#pragma once

#include <QObject>
#include <QString>

class MediaEngine;

class ApplicationController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)

public:
    explicit ApplicationController(QObject *parent = nullptr);

    Q_INVOKABLE bool openFile();
    Q_INVOKABLE bool loadFile(const QString &path);
    Q_INVOKABLE void togglePlayback();
    Q_INVOKABLE void seekTo(double seconds);

    MediaEngine *mediaEngine() const;

    double position() const;
    double duration() const;

signals:
    void requestOpenFile();
    void playbackStateChanged(bool isPlaying);
    void positionChanged(double pos);
    void durationChanged(double dur);

private:
    MediaEngine *m_mediaEngine;
};
