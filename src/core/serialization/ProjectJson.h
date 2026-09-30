// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/project/ProjectData.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>

namespace vedit {

// Version of docs/FILE_FORMAT.md implemented by this build.
inline constexpr int kProjectFormatVersion = 1;
inline constexpr QLatin1StringView kProjectFormatName("vedit.project");

struct ProjectLoadResult
{
    std::optional<ProjectData> project; // empty on error
    QString error;                      // why the project cannot be opened (for the user)
    QStringList warnings;               // values corrected or repaired while loading (for the log)
    int sourceFormatVersion = 0;        // formatVersion found in the file
    bool migrated = false;              // true if migrations were applied

    bool ok() const { return project.has_value(); }
};

// JSON <-> model (docs/FILE_FORMAT.md). Writing is canonical: the same model always produces the same
// bytes (sorted keys, fixed indentation), so projects diff cleanly in git.
namespace projectjson {

QJsonObject toJson(const ProjectData &project);
// A text style (FILE_FORMAT §5.5), e.g. of a style preset: missing or invalid values get their defaults.
QJsonObject textStyleToJson(const TextStyle &style);
TextStyle textStyleFromJson(const QJsonObject &json);
QJsonObject textAnimationToJson(const TextAnimation &animation);
std::optional<TextAnimation> textAnimationFromJson(const QJsonObject &json);
// The settings of an audio visualizer (sticker clips, library presets).
AudioVisualizerSettings visualizerFromJson(const QJsonObject &json);
QJsonObject visualizerToJson(const AudioVisualizerSettings &settings);
// One media item (the probe process sends them to the editor in this form).
QJsonObject mediaToJson(const Media &media);
std::optional<Media> mediaFromJson(const QJsonObject &json, QString *error = nullptr);
QByteArray toBytes(const ProjectData &project);

// Reads any supported format version (older versions are migrated first). Never throws: every problem
// becomes either a warning (value corrected) or an error (project not opened).
ProjectLoadResult fromJson(const QJsonObject &json);
ProjectLoadResult fromBytes(const QByteArray &bytes);

} // namespace projectjson

} // namespace vedit
