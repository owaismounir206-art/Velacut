// SPDX-License-Identifier: GPL-3.0-or-later
#include "Proxy.h"

#include "common/Paths.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace velacut::engine {

QString proxyPath(const Media &media, int height)
{
    return paths::cacheDir() + u"/proxy/"_s + media.fingerprint.value + u"-proxy"_s + QString::number(height) + u".mp4"_s;
}

bool needsProxy(const Media &media)
{
    if (media.kind != MediaKind::Video || media.proxy == ProxyPolicy::Off || !media.info.video || !media.fingerprint.isValid()) {
        return false;
    }
    const VideoStreamInfo &video = *media.info.video;
    const int longSide = std::max(video.width, video.height);
    const int shortSide = std::min(video.width, video.height);
    const QString codec = video.codec.toLower();
    const QString format = video.pixelFormat.toLower();
    const bool heavyCodec = codec == u"hevc"_s || codec == u"h265"_s || codec == u"av1"_s || codec == u"vp9"_s;
    const bool deepColour = format.contains(u"p10"_s) || format.contains(u"p12"_s) || format.contains(u"p16"_s);
    return longSide > 1920 || shortSide > 1080 || heavyCodec || deepColour || video.variableFrameRate;
}

ProxyQueue::ProxyQueue(QObject *parent)
    : MediaCopyQueue(
          u"proxy"_s, [this](const Media &media) { return proxyPath(media, m_height); },
          [this](const Media &media, const QString &output) {
              QStringList arguments{u"--proxy"_s, media.path, u"--output"_s, output, u"--height"_s, QString::number(m_height)};
              if (!m_encoder.isEmpty()) {
                  arguments << u"--encoder"_s << m_encoder;
              }
              return arguments;
          },
          parent)
{
}

} // namespace velacut::engine
