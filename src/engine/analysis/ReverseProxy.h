// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "engine/analysis/MediaCopyQueue.h"

namespace velacut::engine {

// Where the backwards copy of a media item lives (cache, by fingerprint: shared by every project).
QString reverseProxyPath(const Media &media);
bool reverseProxyReady(const Media &media);

// Prepares the backwards copies for the preview of reversed clips, one at a time, in velacut-render (--backwards).
// Until one is ready, the clip still plays backwards, directly from the original (correct but slow).
class ReverseProxyQueue : public MediaCopyQueue
{
    Q_OBJECT

public:
    explicit ReverseProxyQueue(QObject *parent = nullptr);
};

} // namespace velacut::engine
