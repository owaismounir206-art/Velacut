// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/project/Media.h"

#include <QString>

#include <optional>

namespace vedit::engine {

// "sha256-sampled-v1" (docs/FILE_FORMAT.md §7): SHA-256 of the size (uint64 little-endian) + first MiB + 1 MiB from
// floor(size/2) + last MiB; files up to 3 MiB are hashed whole (after the size). A few milliseconds on any file.
std::optional<MediaFingerprint> sampledFingerprint(const QString &path, QString *error = nullptr);

} // namespace vedit::engine
