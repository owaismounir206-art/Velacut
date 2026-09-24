// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/project/Id.h"
#include "core/time/RationalTime.h"

#include <QString>

#include <cstdint>
#include <optional>

namespace vedit {

enum class MediaKind
{
    Video,
    Audio,
    Image,
    ImageSequence,
};

// Sampled content hash used to relink moved media (docs/FILE_FORMAT.md §7).
struct MediaFingerprint
{
    QString algorithm; // "sha256-sampled-v1"
    QString value;     // lowercase hex
    std::int64_t size = 0;

    bool isValid() const { return !algorithm.isEmpty() && !value.isEmpty() && size >= 0; }
    friend bool operator==(const MediaFingerprint &, const MediaFingerprint &) = default;
};

struct VideoStreamInfo
{
    int width = 0;
    int height = 0;
    std::optional<Rational> frameRate;
    bool variableFrameRate = false;
    int rotation = 0; // degrees, clockwise, from container metadata
    QString codec;
    QString pixelFormat;
    QString primaries;
    QString transfer;
    bool hdr = false;
    bool hasAlpha = false;
    Rational sampleAspectRatio{1};

    friend bool operator==(const VideoStreamInfo &, const VideoStreamInfo &) = default;
};

struct AudioStreamInfo
{
    QString codec;
    int sampleRate = 0;
    int channels = 0;

    friend bool operator==(const AudioStreamInfo &, const AudioStreamInfo &) = default;
};

// Probe results cached in the project; re-probed when the fingerprint changes. Missing fields = unknown.
struct MediaInfo
{
    std::optional<RationalTime> duration; // at the media's native rate
    std::optional<VideoStreamInfo> video;
    std::optional<AudioStreamInfo> audio;

    friend bool operator==(const MediaInfo &, const MediaInfo &) = default;
};

enum class ProxyPolicy
{
    Auto,
    Off,
};

struct MediaFolder
{
    FolderId id;
    QString name;
    FolderId parentId; // null = root

    friend bool operator==(const MediaFolder &, const MediaFolder &) = default;
};

struct Media
{
    MediaId id;
    MediaKind kind = MediaKind::Video;
    QString name;
    QString path;         // absolute POSIX path (a printf pattern for image sequences)
    QString relativePath; // relative to the .vproj directory; empty for drafts
    MediaFingerprint fingerprint;
    MediaInfo info;
    ProxyPolicy proxy = ProxyPolicy::Auto;
    bool favorite = false;
    FolderId folderId; // null = root

    friend bool operator==(const Media &, const Media &) = default;
};

} // namespace vedit
