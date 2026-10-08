// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/project/Media.h"

#include <QString>

#include <optional>

namespace velacut::engine {

enum class ProbeError
{
    None,
    Missing,     // no such file
    Unreadable,  // permissions, I/O error
    Unsupported, // not an audio, video or image file FFmpeg can read
    Damaged,     // the probe crashed on it (reported by MediaImporter)
};

struct ProbeResult
{
    std::optional<Media> media; // new id, name, absolute path, kind, fingerprint, info
    ProbeError error = ProbeError::None;
    QString detail; // technical, untranslated
};

// Reads the metadata of a media file with libavformat (docs/ARCHITECTURE.md §5.5). Runs in velacut-render --probe,
// so that a file which crashes the demuxer is rejected instead of crashing the editor (D-07).
ProbeResult probeMedia(const QString &path);

QString probeErrorCode(ProbeError error);
ProbeError probeErrorFromCode(const QString &code);

} // namespace velacut::engine
