#include "Applicationcontroller.h"
#include "MediaEngine.h"

ApplicationController::ApplicationController(QObject *parent)
    : QObject(parent)
    , m_mediaEngine(new MediaEngine(this))
{
}

bool ApplicationController::openFile()
{
    emit requestOpenFile();
    return true;
}

bool ApplicationController::loadFile(const QString &path)
{
    QString filePath = path;
    if (filePath.startsWith("file://"))
        filePath = filePath.mid(7);

    m_mediaEngine->open(filePath);
    return true;
}

MediaEngine *ApplicationController::mediaEngine() const
{
    return m_mediaEngine;
}
