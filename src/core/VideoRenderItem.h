#pragma once

#include <QQuickPaintedItem>
#include <QImage>
#include <qqml.h>

class VideoRenderItem : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QImage image READ image WRITE setImage NOTIFY imageChanged)

public:
    explicit VideoRenderItem(QQuickItem *parent = nullptr);

    void paint(QPainter *painter) override;

    QImage image() const;
    void setImage(const QImage &image);

signals:
    void imageChanged();

private:
    QImage m_image;
};
