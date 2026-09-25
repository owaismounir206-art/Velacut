// SPDX-License-Identifier: GPL-3.0-or-later
#include "WaveformView.h"

#include "controllers/EditorController.h"
#include "engine/analysis/MediaAnalysis.h"

#include <QPainter>
#include <QPainterPath>

#include <cmath>

namespace vedit::ui {

WaveformView::WaveformView(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setOpaquePainting(false);
}

WaveformView::~WaveformView() = default;

void WaveformView::setEditor(EditorController *editor)
{
    if (editor == m_editor) {
        return;
    }
    disconnect(m_readyConnection);
    m_editor = editor;
    if (m_editor) {
        m_readyConnection = connect(&m_editor->analysis(), &engine::MediaAnalysis::waveformReady, this,
                                    [this](const MediaId &id) {
                                        if (id.toString() == m_mediaId) {
                                            reload();
                                        }
                                    });
    }
    emit editorChanged();
    reload();
}

void WaveformView::setMediaId(const QString &mediaId)
{
    if (mediaId != m_mediaId) {
        m_mediaId = mediaId;
        emit mediaIdChanged();
        reload();
    }
}

void WaveformView::setSourceIn(int frames)
{
    if (frames != m_sourceIn) {
        m_sourceIn = frames;
        emit sourceChanged();
        update();
    }
}

void WaveformView::setSourceDuration(int frames)
{
    if (frames != m_sourceDuration) {
        m_sourceDuration = frames;
        emit sourceChanged();
        update();
    }
}

void WaveformView::setColor(const QColor &color)
{
    if (color != m_color) {
        m_color = color;
        emit colorChanged();
        update();
    }
}

void WaveformView::reload()
{
    const Media *media = m_editor ? m_editor->findMedia(m_mediaId) : nullptr;
    m_waveform = media ? m_editor->analysis().waveform(*media) : nullptr;
    update();
}

void WaveformView::paint(QPainter *painter)
{
    if (!m_waveform || m_sourceDuration <= 0 || width() <= 0 || height() <= 0 || !m_editor) {
        return;
    }
    const QByteArray &peaks = m_waveform->peaks;
    const int buckets = m_waveform->bucketCount();
    const double bucketsPerFrame = m_waveform->bucketsPerSecond / m_editor->frameRate();
    const double middle = height() / 2.0;
    const int columns = static_cast<int>(std::ceil(width()));
    QPainterPath path;
    for (int x = 0; x < columns; ++x) {
        const double from = (m_sourceIn + m_sourceDuration * x / width()) * bucketsPerFrame;
        const double to = (m_sourceIn + m_sourceDuration * (x + 1) / width()) * bucketsPerFrame;
        int low = 0;
        int high = 0;
        for (int b = static_cast<int>(from); b < std::max(static_cast<int>(from) + 1, static_cast<int>(to)) && b < buckets; ++b) {
            if (b >= 0) {
                low = std::min<int>(low, static_cast<signed char>(peaks[2 * b]));
                high = std::max<int>(high, static_cast<signed char>(peaks[2 * b + 1]));
            }
        }
        // At least one pixel, so silence still shows as a line.
        const double top = middle - std::max(0.5, high / 127.0 * middle);
        const double bottom = middle + std::max(0.5, -low / 127.0 * middle);
        path.addRect(QRectF(x, top, 1.0, bottom - top));
    }
    painter->fillPath(path, m_color);
}

} // namespace vedit::ui
