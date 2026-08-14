#pragma once

#include <QQuickRhiItem>
#include <QByteArray>
#include <QMutex>
#include <QSize>
#include <QString>
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
    // 画面调节：亮度/对比度/饱和度，范围 -100~100（0 = 原始画面）
    Q_PROPERTY(int brightness READ brightness WRITE setBrightness NOTIFY brightnessChanged)
    Q_PROPERTY(int contrast READ contrast WRITE setContrast NOTIFY contrastChanged)
    Q_PROPERTY(int saturation READ saturation WRITE setSaturation NOTIFY saturationChanged)
    // 缩放模式：0 = Fit（完整显示留边） 1 = Fill（铺满裁剪） 2 = Stretch（拉伸变形）
    Q_PROPERTY(int scaleMode READ scaleMode WRITE setScaleMode NOTIFY scaleModeChanged)

public:
    explicit VideoRenderItem(QQuickItem *parent = nullptr);

    Q_INVOKABLE void setYUVFrame(const YUVFrame &frame);
    Q_INVOKABLE void clearImage();
    Q_INVOKABLE QString captureAndSave(const QString &savePath = QString(), const QString &baseName = QString());

    int videoRotation() const { return m_videoRotation; }
    void setVideoRotation(int angle);
    bool flipVertical() const { return m_flipVertical; }
    void setFlipVertical(bool flip);

    int brightness() const { return m_brightness; }
    void setBrightness(int value);
    int contrast() const { return m_contrast; }
    void setContrast(int value);
    int saturation() const { return m_saturation; }
    void setSaturation(int value);
    int scaleMode() const { return m_scaleMode; }
    void setScaleMode(int mode);

signals:
    void videoRotationChanged();
    void flipVerticalChanged();
    void brightnessChanged();
    void contrastChanged();
    void saturationChanged();
    void scaleModeChanged();

protected:
    QQuickRhiItemRenderer *createRenderer() override;

private:
    friend class VideoRenderItemRenderer;
    YUVFrame m_pendingFrame;
    bool m_clearRequested{false};
    QMutex m_mutex;
    int m_videoRotation{0};
    bool m_flipVertical{false};
    int m_brightness{0};
    int m_contrast{0};
    int m_saturation{0};
    int m_scaleMode{0};
};
