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
    // 画面旋转角度 (0/90/180/270)，避开 QQuickItem 自带的 qreal rotation 属性
    Q_PROPERTY(int videoRotation READ videoRotation WRITE setVideoRotation NOTIFY videoRotationChanged)
    Q_PROPERTY(bool flipVertical READ flipVertical WRITE setFlipVertical NOTIFY flipVerticalChanged)

public:
    explicit VideoRenderItem(QQuickItem *parent = nullptr);

    Q_INVOKABLE void setYUVFrame(const YUVFrame &frame);
    Q_INVOKABLE void clearImage();

    int videoRotation() const { return m_videoRotation; }
    void setVideoRotation(int angle);
    bool flipVertical() const { return m_flipVertical; }
    void setFlipVertical(bool flip);

signals:
    void videoRotationChanged();
    void flipVerticalChanged();

protected:
    QQuickRhiItemRenderer *createRenderer() override;

private:
    friend class VideoRenderItemRenderer;
    YUVFrame m_pendingFrame;
    bool m_clearRequested{false};
    QMutex m_mutex;
    int m_videoRotation{0};
    bool m_flipVertical{false};
};
