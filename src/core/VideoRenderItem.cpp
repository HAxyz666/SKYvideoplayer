#include "VideoRenderItem.h"

#include <qrhi.h>
#include <qshader.h>
#include <QFile>

static QShader loadShader(const QString &path)
{
    QFile f(path);
    if (f.open(QIODevice::ReadOnly))
        return QShader::fromSerialized(f.readAll());
    return {};
}

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

    explicit VideoRenderItemRenderer(QMutex &mtx) : mutex(mtx) {}

    ~VideoRenderItemRenderer()
    {
        if (m_initialUpdates)
            m_initialUpdates->release();
        delete pipeline;     pipeline = nullptr;
        delete srb;          srb = nullptr;
        delete texY;         texY = nullptr;
        delete texU;         texU = nullptr;
        delete texV;         texV = nullptr;
        delete samplerLinear;samplerLinear = nullptr;
        delete vertexBuf;    vertexBuf = nullptr;
        delete uniformBuf;   uniformBuf = nullptr;
    }

    void synchronize(QQuickRhiItem *item) override
    {
        auto *ri = static_cast<VideoRenderItem *>(item);
        QMutexLocker lock(&ri->m_mutex);
        if (!ri->m_pendingFrame.yPlane.isEmpty()) {
            pendingFrame = ri->m_pendingFrame;
            needsUpload = true;
        }
        itemSize = item->size();
    }

    void initialize(QRhiCommandBuffer *cb) override
    {
        if (initialized)
            return;

        QRhi *rhi = cb->rhi();

        samplerLinear = rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                        QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
        samplerLinear->create();

        uniformBuf = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 64);
        uniformBuf->create();

        vertexBuf = rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(kQuadVertices));
        vertexBuf->create();

        m_initialUpdates = rhi->nextResourceUpdateBatch();
        m_initialUpdates->uploadStaticBuffer(vertexBuf, kQuadVertices);

        initialized = true;
    }

    void render(QRhiCommandBuffer *cb) override
    {
        if (pendingFrame.yPlane.isEmpty() || !initialized || !uniformBuf)
            return;

        QRhi *rhi = cb->rhi();
        QRhiResourceUpdateBatch *batch = rhi->nextResourceUpdateBatch();

        if (m_initialUpdates) {
            batch->merge(m_initialUpdates);
            m_initialUpdates->release();
            m_initialUpdates = nullptr;
        }

        if (needsUpload) {
            int w = pendingFrame.frameSize.width();
            int h = pendingFrame.frameSize.height();
            int halfW = (w + 1) / 2;
            int halfH = (h + 1) / 2;

            if (texSize != pendingFrame.frameSize) {
                texSize = pendingFrame.frameSize;
                delete texY;
                texY = rhi->newTexture(QRhiTexture::R8, QSize(w, h), 1);
                texY->create();
                delete texU;
                texU = rhi->newTexture(QRhiTexture::R8, QSize(halfW, halfH), 1);
                texU->create();
                delete texV;
                texV = rhi->newTexture(QRhiTexture::R8, QSize(halfW, halfH), 1);
                texV->create();
                buildPipeline(rhi);
            }

            auto uploadPlane = [&](QRhiTexture *tex, const QByteArray &data) {
                QRhiTextureSubresourceUploadDescription desc(data.constData(), data.size());
                batch->uploadTexture(tex, QRhiTextureUploadEntry(0, 0, desc));
            };
            uploadPlane(texY, pendingFrame.yPlane);
            uploadPlane(texU, pendingFrame.uPlane);
            uploadPlane(texV, pendingFrame.vPlane);

            needsUpload = false;
        }

        batch->updateDynamicBuffer(uniformBuf, 0, 64, mvpMatrix().constData());

        cb->beginPass(renderTarget(), Qt::black, QRhiDepthStencilClearValue(), batch);
        cb->setGraphicsPipeline(pipeline);
        cb->setShaderResources(srb);
        QRhiCommandBuffer::VertexInput vbuf(vertexBuf, 0);
        cb->setVertexInput(0, 1, &vbuf);
        cb->draw(4);
        cb->endPass();
    }

private:
    QMatrix4x4 mvpMatrix() const
    {
        QMatrix4x4 mvp;
        QSizeF ts = pendingFrame.frameSize;
        if (ts.width() > 0 && ts.height() > 0 && itemSize.height() > 0) {
            float ar = float(ts.width()) / ts.height();
            float ir = float(itemSize.width()) / itemSize.height();
            if (ar > ir) mvp.scale(1.0f, ir / ar);
            else         mvp.scale(ar / ir, 1.0f);
        }
        return mvp;
    }

    void buildPipeline(QRhi *rhi)
    {
        delete srb;
        delete pipeline;

        QRhiRenderPassDescriptor *rpDesc = renderTarget()->renderPassDescriptor();

        QRhiVertexInputBinding binding(sizeof(float) * 4);
        QRhiVertexInputAttribute posAttr(0, 0, QRhiVertexInputAttribute::Float2, 0);
        QRhiVertexInputAttribute texAttr(0, 1, QRhiVertexInputAttribute::Float2, sizeof(float) * 2);
        QRhiVertexInputLayout inputLayout;
        inputLayout.setBindings({binding});
        inputLayout.setAttributes({posAttr, texAttr});

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
    update();
}

void VideoRenderItem::clearImage()
{
    QMutexLocker lock(&m_mutex);
    m_pendingFrame = YUVFrame();
    update();
}
