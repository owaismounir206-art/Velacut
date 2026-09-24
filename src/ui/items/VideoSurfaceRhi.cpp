// SPDX-License-Identifier: GPL-3.0-or-later
#include "VideoSurfaceRhi.h"

#include "items/VideoFit.h"

#include <QFile>
#include <QMatrix4x4>
#include <rhi/qrhi.h>

#include <memory>

namespace vedit::ui {

namespace {

QShader loadShader(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QShader::fromSerialized(file.readAll()) : QShader();
}

// Triangle strip: (x, y) in GL-style clip space (y up), (u, v) with v = 0 at the top of the image.
constexpr float kQuad[] = {
    -1.0f, -1.0f, 0.0f, 1.0f, //
    1.0f,  -1.0f, 1.0f, 1.0f, //
    -1.0f, 1.0f,  0.0f, 0.0f, //
    1.0f,  1.0f,  1.0f, 0.0f, //
};

class Renderer : public QQuickRhiItemRenderer
{
public:
    void initialize(QRhiCommandBuffer *) override
    {
        if (m_rhi != rhi()) {
            m_pipeline.reset();
            m_srb.reset();
            m_texture.reset();
            m_sampler.reset();
            m_vertices.reset();
            m_uniforms.reset();
            m_rhi = rhi();
            m_uploadVertices = true;
        }
        if (m_pipeline) {
            return;
        }
        m_vertices.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(kQuad)));
        m_vertices->create();
        m_uniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 64));
        m_uniforms->create();
        m_sampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                          QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        m_sampler->create();
        // Placeholder until the first frame arrives, so the resource bindings are always complete.
        m_texture.reset(m_rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
        m_texture->create();
        m_hasFrame = false;
        m_srb.reset(m_rhi->newShaderResourceBindings());
        updateBindings();
        m_pipeline.reset(m_rhi->newGraphicsPipeline());
        m_pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
        m_pipeline->setShaderStages({{QRhiShaderStage::Vertex, loadShader(QStringLiteral(":/vedit/shaders/preview.vert.qsb"))},
                                     {QRhiShaderStage::Fragment, loadShader(QStringLiteral(":/vedit/shaders/preview.frag.qsb"))}});
        QRhiVertexInputLayout layout;
        layout.setBindings({{4 * sizeof(float)}});
        layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float4, 0}});
        m_pipeline->setVertexInputLayout(layout);
        m_pipeline->setSampleCount(renderTarget()->sampleCount());
        m_pipeline->setShaderResourceBindings(m_srb.get());
        m_pipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
        m_pipeline->create();
    }

    void synchronize(QQuickRhiItem *item) override
    {
        auto *surface = static_cast<VideoSurfaceRhi *>(item);
        m_background = surface->backgroundColor();
        engine::FrameSink *sink = surface->sink();
        if (!sink) {
            return;
        }
        quint64 serial = 0;
        QImage image = sink->latest(&serial);
        if (serial != m_serial && !image.isNull()) {
            m_pending = std::move(image);
            m_serial = serial;
            sink->markConsumed(serial);
        }
    }

    void render(QRhiCommandBuffer *cb) override
    {
        QRhiResourceUpdateBatch *updates = m_rhi->nextResourceUpdateBatch();
        if (m_uploadVertices) {
            updates->uploadStaticBuffer(m_vertices.get(), kQuad);
            m_uploadVertices = false;
        }
        if (!m_pending.isNull()) {
            if (m_texture->pixelSize() != m_pending.size()) {
                m_texture.reset(m_rhi->newTexture(QRhiTexture::RGBA8, m_pending.size()));
                m_texture->create();
                updateBindings();
            }
            updates->uploadTexture(m_texture.get(), m_pending);
            m_frameSize = m_pending.size();
            m_hasFrame = true;
            m_pending = QImage();
        }
        const QSize target = renderTarget()->pixelSize();
        const QSizeF scale = m_hasFrame ? fitScale(m_frameSize, target) : QSizeF(1.0, 1.0);
        QMatrix4x4 mvp = m_rhi->clipSpaceCorrMatrix();
        mvp.scale(static_cast<float>(scale.width()), static_cast<float>(scale.height()));
        updates->updateDynamicBuffer(m_uniforms.get(), 0, 64, mvp.constData());

        cb->beginPass(renderTarget(), m_background, {1.0f, 0}, updates);
        if (m_hasFrame) {
            cb->setGraphicsPipeline(m_pipeline.get());
            cb->setViewport({0, 0, float(target.width()), float(target.height())});
            cb->setShaderResources();
            const QRhiCommandBuffer::VertexInput input(m_vertices.get(), 0);
            cb->setVertexInput(0, 1, &input);
            cb->draw(4);
        }
        cb->endPass();
    }

private:
    void updateBindings()
    {
        m_srb->setBindings({QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage, m_uniforms.get()),
                            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                                      m_texture.get(), m_sampler.get())});
        m_srb->create();
    }

    QRhi *m_rhi = nullptr;
    std::unique_ptr<QRhiBuffer> m_vertices;
    std::unique_ptr<QRhiBuffer> m_uniforms;
    std::unique_ptr<QRhiSampler> m_sampler;
    std::unique_ptr<QRhiTexture> m_texture;
    std::unique_ptr<QRhiShaderResourceBindings> m_srb;
    std::unique_ptr<QRhiGraphicsPipeline> m_pipeline;
    bool m_uploadVertices = true;
    bool m_hasFrame = false;
    QImage m_pending;
    QSize m_frameSize;
    quint64 m_serial = 0;
    QColor m_background = Qt::black;
};

} // namespace

VideoSurfaceRhi::VideoSurfaceRhi(QQuickItem *parent)
    : QQuickRhiItem(parent)
{
}

void VideoSurfaceRhi::setSink(engine::FrameSink *sink)
{
    if (sink == m_sink) {
        return;
    }
    if (m_sink) {
        disconnect(m_sink, nullptr, this, nullptr);
    }
    m_sink = sink;
    if (m_sink) {
        connect(m_sink, &engine::FrameSink::frameReady, this, &QQuickItem::update);
    }
    emit sinkChanged();
    update();
}

void VideoSurfaceRhi::setBackgroundColor(const QColor &color)
{
    if (color != m_backgroundColor) {
        m_backgroundColor = color;
        emit backgroundColorChanged();
        update();
    }
}

QQuickRhiItemRenderer *VideoSurfaceRhi::createRenderer()
{
    return new Renderer;
}

} // namespace vedit::ui
