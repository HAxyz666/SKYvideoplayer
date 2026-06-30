#pragma once

#include <QQuickRhiItem>
#include <QByteArray>
#include <QMutex>
#include <QSize>
#include <QtQml/qqmlregistration.h>

struct YUVFrame {
    QByteArray yPlane;
    QByteArray uPlane;
    QByteArray vPlane;
    QSize frameSize;
};
Q_DECLARE_METATYPE(YUVFrame)

class VideoRenderItem : public QQuickRhiItem
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit VideoRenderItem(QQuickItem *parent = nullptr);

    Q_INVOKABLE void setYUVFrame(const YUVFrame &frame);
    Q_INVOKABLE void clearImage();

protected:
    QQuickRhiItemRenderer *createRenderer() override;

private:
    friend class VideoRenderItemRenderer;
    YUVFrame m_pendingFrame;
    bool m_clearRequested{false};
    QMutex m_mutex;
};
