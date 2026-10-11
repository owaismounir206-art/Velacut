// SPDX-License-Identifier: GPL-3.0-or-later
#include "ReverseProxy.h"

#include "common/Paths.h"

#include <QFileInfo>

using namespace Qt::StringLiterals;

namespace velacut::engine {

QString reverseProxyPath(const Media &media)
{
    return paths::cacheDir() + u"/proxy/"_s + media.fingerprint.value + u"-reverse.mp4"_s;
}

bool reverseProxyReady(const Media &media)
{
    return media.fingerprint.isValid() && QFileInfo::exists(reverseProxyPath(media));
}

ReverseProxyQueue::ReverseProxyQueue(QObject *parent)
    : MediaCopyQueue(u"backwards copy"_s, reverseProxyPath,
                     [](const Media &media, const QString &output) {
                         return QStringList{u"--backwards"_s, media.path, u"--output"_s, output};
                     },
                     parent)
{
}

} // namespace velacut::engine
