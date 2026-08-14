#include "VideoRenderItem.h"

#include <qrhi.h>
#include <qshader.h>
#include <QFile>
#include <QImage>
#include <QDir>
#include <QDateTime>

extern "C" {
#include <libswscale/swscale.h>
}

static QShader loadShader(const QString &path)
{
    QFile f(path);
    if (f.open(QIODevice::ReadOnly))
        return QShader::fromSerialized(f.readAll());
    return {};
}


//周代森：顶点定义：矩形，图片位置匹配
static const float kQuadVertices[] = {
    -1.0f, -1.0f,  0.0f, 1.0f,
     1.0f, -1.0f,  1.0f, 1.0f,
    -1.0f,  1.0f,  0.0f, 0.0f,
     1.0f,  1.0f,  1.0f, 0.0f,
};

class VideoRenderItemRenderer : public QQuickRhiItemRenderer
{
public:
    YUVFrame pendingFrame;
    QMutex &mutex;
    bool needsUpload = false;
    int videoRotation = 0;
    bool flipVertical = false;
    int brightness = 0;
    int contrast = 0;
    int saturation = 0;
    int scaleMode = 0;

    explicit VideoRenderItemRenderer(QMutex &mtx) : mutex(mtx) {}

    ~VideoRenderItemRenderer()
    {
        if (m_initialUpdates)
            m_initialUpdates->release();
        if (pipeline) { pipeline->destroy(); delete pipeline; pipeline = nullptr; }
        if (srb) { srb->destroy(); delete srb; srb = nullptr; }
        if (texY) { texY->destroy(); delete texY; texY = nullptr; }
        if (texU) { texU->destroy(); delete texU; texU = nullptr; }
        if (texV) { texV->destroy(); delete texV; texV = nullptr; }
        if (samplerLinear) { samplerLinear->destroy(); delete samplerLinear; samplerLinear = nullptr; }
        if (vertexBuf) { vertexBuf->destroy(); delete vertexBuf; vertexBuf = nullptr; }
        if (uniformBuf) { uniformBuf->destroy(); delete uniformBuf; uniformBuf = nullptr; }
    }

    void synchronize(QQuickRhiItem *item) override
    {
        auto *ri = static_cast<VideoRenderItem *>(item);
        QMutexLocker lock(&ri->m_mutex);
        if (ri->m_clearRequested) {
            pendingFrame = YUVFrame();
            needsUpload = false;
            ri->m_clearRequested = false;
        } else if (!ri->m_pendingFrame.yPlane.isEmpty()) {
            pendingFrame = ri->m_pendingFrame;
            needsUpload = true;
        }
        // 画面旋转 / 翻转状态同步到渲染线程
        videoRotation = ri->m_videoRotation;
        flipVertical = ri->m_flipVertical;
        brightness = ri->m_brightness;
        contrast = ri->m_contrast;
        saturation = ri->m_saturation;
        scaleMode = ri->m_scaleMode;
        itemSize = item->size();
    }

    void initialize(QRhiCommandBuffer *cb) override
    {
        if (initialized)
            return;

        QRhi *rhi = cb->rhi();

        //周代森：创建采样器，控制纹理读取（1,2线性插值保证画面平滑；3不使用mipmap；4,5,6裁剪范围以外的画面）
        samplerLinear = rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                        QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
        samplerLinear->create();

        //周代森：创建Uniform缓冲，存放着色器的常量数据（1每帧都更新mvp矩阵；2用途：着色器常量缓冲；3，大小）
        // 布局：mat4 mvp (64B) + vec4 画面调节参数 (16B)，std140 对齐
        uniformBuf = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 80);
        uniformBuf->create();

        //周代森：创建顶点缓冲，存放画面四个顶点数据（1顶点固定不变；2用途：作为顶点输出；3四个顶点[矩形画面]）
        vertexBuf = rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(kQuadVertices));
        vertexBuf->create();

        m_initialUpdates = rhi->nextResourceUpdateBatch();
        m_initialUpdates->uploadStaticBuffer(vertexBuf, kQuadVertices);

        initialized = true;
    }

    void render(QRhiCommandBuffer *cb) override
    {
        if (!initialized || !uniformBuf)
            return;

        QRhi *rhi = cb->rhi();
        //周代森：创建资源更新批处理对象，将需要更新的内容先暂存，后续一起发送
        QRhiResourceUpdateBatch *batch = rhi->nextResourceUpdateBatch();

        if (m_initialUpdates) {
            batch->merge(m_initialUpdates);
            m_initialUpdates->release();
            m_initialUpdates = nullptr;
        }

        const bool hasFrameData = !pendingFrame.yPlane.isEmpty();

        // Upload must be attempted even when texY/pipeline are still null
        // (first valid frame), otherwise textures and pipeline are never
        // created and the screen stays black forever.
        if (needsUpload && hasFrameData) {
            int w = pendingFrame.frameSize.width();
            int h = pendingFrame.frameSize.height();
            int halfW = (w + 1) / 2;
            int halfH = (h + 1) / 2;

            if (texSize != pendingFrame.frameSize) {
                texSize = pendingFrame.frameSize;
                if (texY) { texY->destroy(); delete texY; }
                texY = rhi->newTexture(QRhiTexture::R8, QSize(w, h), 1);
                texY->create();
                if (texU) { texU->destroy(); delete texU; }
                texU = rhi->newTexture(QRhiTexture::R8, QSize(halfW, halfH), 1);
                texU->create();
                if (texV) { texV->destroy(); delete texV; }
                texV = rhi->newTexture(QRhiTexture::R8, QSize(halfW, halfH), 1);
                texV->create();
                buildPipeline(rhi);
            }

            //周代森：将数据上传为GPU纹理
            auto uploadPlane = [&](QRhiTexture *tex, const QByteArray &data) {
                QRhiTextureSubresourceUploadDescription desc(data.constData(), data.size());
                batch->uploadTexture(tex, QRhiTextureUploadEntry(0, 0, desc));
            };
            uploadPlane(texY, pendingFrame.yPlane);
            uploadPlane(texU, pendingFrame.uPlane);
            uploadPlane(texV, pendingFrame.vPlane);

            needsUpload = false;
        }

        const bool hasFrame = hasFrameData && pipeline && texY;

        if (hasFrame) {
            batch->updateDynamicBuffer(uniformBuf, 0, 64, mvpMatrix().constData());
            // 画面调节参数：[-100,100] 归一化到 shader 期望区间
            const float params[4] = {
                brightness / 100.0f,
                (contrast + 100.0f) / 100.0f,
                (saturation + 100.0f) / 100.0f,
                0.0f
            };
            batch->updateDynamicBuffer(uniformBuf, 64, 16, params);
        }

        //周代森：强制渲染当前画面，修复了暂停+按下全屏时的画面尺寸问题
        // beginPass always clears to Qt::black, so an empty frame (e.g. after
        // clearImage()) simply repaints the surface black instead of leaving
        // the previous frame stuck on screen.
        cb->beginPass(renderTarget(), Qt::black, QRhiDepthStencilClearValue(), batch);
        if (hasFrame) {
            QSize s = renderTarget()->pixelSize();
            cb->setViewport(QRhiViewport(0, 0, s.width(), s.height()));
            cb->setScissor(QRhiScissor(0, 0, s.width(), s.height()));
            cb->setGraphicsPipeline(pipeline);
            cb->setShaderResources(srb);
            QRhiCommandBuffer::VertexInput vbuf(vertexBuf, 0);
            cb->setVertexInput(0, 1, &vbuf);
            cb->draw(4);
        }
        cb->endPass();
    }

private:
    QMatrix4x4 mvpMatrix() const
    {
        QMatrix4x4 mvp;
        QSizeF ts = pendingFrame.frameSize;
        if (ts.width() > 0 && ts.height() > 0 && itemSize.height() > 0) {
            // 旋转 90/270 后画面宽高互换，按旋转后的有效尺寸做适配计算
            QSizeF effective = ts;
            if (videoRotation == 90 || videoRotation == 270)
                effective = QSizeF(ts.height(), ts.width());

            float ar = float(effective.width()) / effective.height();
            float ir = float(itemSize.width()) / itemSize.height();
            // 缩放模式：Fit 完整显示留边；Fill 铺满裁剪；Stretch 拉伸变形
            if (scaleMode == 2) {
                // 不缩放
            } else if (ar > ir) {
                if (scaleMode == 1) mvp.scale(ar / ir, 1.0f);
                else                mvp.scale(1.0f, ir / ar);
            } else {
                if (scaleMode == 1) mvp.scale(1.0f, ir / ar);
                else                mvp.scale(ar / ir, 1.0f);
            }

            // 变换作用于顶点顺序：翻转 → 旋转 → 适配缩放
            if (videoRotation != 0)
                mvp.rotate(float(videoRotation), 0.0f, 0.0f, 1.0f);

            if (flipVertical)
                mvp.scale(1.0f, -1.0f);
        }
        return mvp;
    }

    void buildPipeline(QRhi *rhi)
    {
        if (srb) { srb->destroy(); delete srb; srb = nullptr; }
        if (pipeline) { pipeline->destroy(); delete pipeline; pipeline = nullptr; }

        //周代森：获取当前渲染目标的渲染信息
        QRhiRenderPassDescriptor *rpDesc = renderTarget()->renderPassDescriptor();

        QRhiVertexInputBinding binding(sizeof(float) * 4);
        QRhiVertexInputAttribute posAttr(0, 0, QRhiVertexInputAttribute::Float2, 0);
        QRhiVertexInputAttribute texAttr(0, 1, QRhiVertexInputAttribute::Float2, sizeof(float) * 2);
        QRhiVertexInputLayout inputLayout;
        inputLayout.setBindings({binding});
        inputLayout.setAttributes({posAttr, texAttr});

        //周代森：加载着色器
        QShader vertShader = loadShader(":/shaders/yuv.vert.qsb");
        vertShader.setStage(QShader::VertexStage);
        QShader fragShader = loadShader(":/shaders/yuv.frag.qsb");
        fragShader.setStage(QShader::FragmentStage);

        srb = rhi->newShaderResourceBindings();
        srb->setBindings({
            QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage, texY, samplerLinear),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, texU, samplerLinear),
            QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage, texV, samplerLinear),
            QRhiShaderResourceBinding::uniformBuffer(3,
                QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, uniformBuf),
        });
        srb->create();

        //周代森：创建图形管线并设置着色器
        pipeline = rhi->newGraphicsPipeline();
        pipeline->setShaderStages({
            QRhiShaderStage(QRhiShaderStage::Vertex, vertShader),
            QRhiShaderStage(QRhiShaderStage::Fragment, fragShader)
        });
        pipeline->setVertexInputLayout(inputLayout);
        pipeline->setRenderPassDescriptor(rpDesc);
        pipeline->setShaderResourceBindings(srb);
        pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);

        if (!pipeline->create())
            qWarning("[VR] pipeline create failed");
    }

    QRhiTexture *texY = nullptr;
    QRhiTexture *texU = nullptr;
    QRhiTexture *texV = nullptr;
    QRhiSampler *samplerLinear = nullptr;
    QRhiGraphicsPipeline *pipeline = nullptr;
    QRhiBuffer *vertexBuf = nullptr;
    QRhiBuffer *uniformBuf = nullptr;
    QRhiShaderResourceBindings *srb = nullptr;
    QRhiResourceUpdateBatch *m_initialUpdates = nullptr;
    QSizeF itemSize;
    QSize texSize;
    bool initialized = false;
};

VideoRenderItem::VideoRenderItem(QQuickItem *parent)
    : QQuickRhiItem(parent)
{
}

QQuickRhiItemRenderer *VideoRenderItem::createRenderer()
{
    return new VideoRenderItemRenderer(m_mutex);
}

void VideoRenderItem::setYUVFrame(const YUVFrame &frame)
{
    QMutexLocker lock(&m_mutex);
    m_pendingFrame = frame;
    m_clearRequested = false;
    update();
}

void VideoRenderItem::clearImage()
{
    QMutexLocker lock(&m_mutex);
    m_pendingFrame = YUVFrame();
    m_clearRequested = true;
    update();
}

void VideoRenderItem::setVideoRotation(int angle)
{
    // 规范到 [0, 360)
    int normalized = angle % 360;
    if (normalized < 0) normalized += 360;
    if (m_videoRotation == normalized) return;
    m_videoRotation = normalized;
    emit videoRotationChanged();
    update();
}

void VideoRenderItem::setFlipVertical(bool flip)
{
    if (m_flipVertical == flip) return;
    m_flipVertical = flip;
    emit flipVerticalChanged();
    update();
}

void VideoRenderItem::setBrightness(int value)
{
    int v = qBound(-100, value, 100);
    if (m_brightness == v) return;
    m_brightness = v;
    emit brightnessChanged();
    update();
}

void VideoRenderItem::setContrast(int value)
{
    int v = qBound(-100, value, 100);
    if (m_contrast == v) return;
    m_contrast = v;
    emit contrastChanged();
    update();
}

void VideoRenderItem::setSaturation(int value)
{
    int v = qBound(-100, value, 100);
    if (m_saturation == v) return;
    m_saturation = v;
    emit saturationChanged();
    update();
}

void VideoRenderItem::setScaleMode(int mode)
{
    int m = qBound(0, mode, 2);
    if (m_scaleMode == m) return;
    m_scaleMode = m;
    emit scaleModeChanged();
    update();
}

QString VideoRenderItem::captureAndSave(const QString &savePath, const QString &baseName)
{
    QMutexLocker lock(&m_mutex);
    if (m_pendingFrame.yPlane.isEmpty())
        return QString();

    int w = m_pendingFrame.frameSize.width();
    int h = m_pendingFrame.frameSize.height();
    if (w <= 0 || h <= 0)
        return QString();

    SwsContext *sws = sws_getContext(w, h, AV_PIX_FMT_YUV420P,
                                      w, h, AV_PIX_FMT_RGBA,
                                      SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws)
        return QString();

    QImage image(w, h, QImage::Format_RGBA8888);
    const uchar *src[4] = {
        reinterpret_cast<const uchar *>(m_pendingFrame.yPlane.constData()),
        reinterpret_cast<const uchar *>(m_pendingFrame.uPlane.constData()),
        reinterpret_cast<const uchar *>(m_pendingFrame.vPlane.constData()),
        nullptr
    };
    int srcStride[4] = { w, (w + 1) / 2, (w + 1) / 2, 0 };
    uint8_t *dst[1] = { image.bits() };
    int dstStride[1] = { w * 4 };

    sws_scale(sws, src, srcStride, 0, h, dst, dstStride);
    sws_freeContext(sws);

    QString path = savePath;
    if (!path.endsWith('/'))
        path += '/';
    QString prefix = baseName.isEmpty() ? "screenshot_" : baseName + "_";
    path += prefix + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".png";

    if (image.save(path, "PNG"))
        return path;
    return QString();
}


