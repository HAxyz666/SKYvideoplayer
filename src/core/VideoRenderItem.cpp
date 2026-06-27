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
        uniformBuf = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 64);
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

        if (hasFrame)
            batch->updateDynamicBuffer(uniformBuf, 0, 64, mvpMatrix().constData());

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
            float ar = float(ts.width()) / ts.height();
            float ir = float(itemSize.width()) / itemSize.height();
            if (ar > ir) mvp.scale(1.0f, ir / ar);
            else         mvp.scale(ar / ir, 1.0f);
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


