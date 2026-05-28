#include "VideoRenderItem.h"
#include <QPainter>

VideoRenderItem::VideoRenderItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
}

void VideoRenderItem::paint(QPainter *painter)
{
    if (m_image.isNull())
        return;

    QSize scaledSize = m_image.size().scaled(static_cast<int>(width()), static_cast<int>(height()), Qt::KeepAspectRatio);
    int x = (static_cast<int>(width()) - scaledSize.width()) / 2;
    int y = (static_cast<int>(height()) - scaledSize.height()) / 2;

    painter->drawImage(QRect(x, y, scaledSize.width(), scaledSize.height()), m_image);
}

QImage VideoRenderItem::image() const
{
    return m_image;
}

void VideoRenderItem::setImage(const QImage &image)
{
    if (m_image == image)
        return;
    m_image = image;
    emit imageChanged();
    update();
}
