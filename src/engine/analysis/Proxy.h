// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "engine/analysis/MediaCopyQueue.h"

namespace velacut::engine {

// Proxies (docs/ARCHITECTURE.md §5.5): light H.264 copies of heavy media files, made in background and read by the
// preview instead of the originals; the export always reads the originals. In the cache, by fingerprint and height
// (shared by every project): <cache>/proxy/<fingerprint>-proxy<height>.mp4.
QString proxyPath(const Media &media, int height);
// A video larger than 1080p, in HEVC, AV1 or VP9, with more than 8 bits, or with a variable frame rate: the files whose
// decoding or seeking makes the preview stutter. Never when the media item's policy is "off" (FILE_FORMAT §5).
bool needsProxy(const Media &media);

class ProxyQueue : public MediaCopyQueue
{
    Q_OBJECT

public:
    explicit ProxyQueue(QObject *parent = nullptr);

    // 720 by default; 540 with software rendering (SPEC 1bis).
    void setHeight(int height) { m_height = height; }
    int height() const { return m_height; }
    // A GPU H.264 encoder verified by the GPU probe ("h264_vaapi"…), tried before libx264.
    void setEncoder(const QString &encoder) { m_encoder = encoder; }

private:
    int m_height = 720;
    QString m_encoder;
};

} // namespace velacut::engine
