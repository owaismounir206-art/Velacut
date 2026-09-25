// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/project/ProjectData.h"
#include "engine/render/ExportSettings.h"

#include <QStringList>

#include <atomic>
#include <functional>

namespace vedit::engine {

// Renders a sequence to a file with the same projection code as the preview (docs/ARCHITECTURE.md §5.4).
// Blocking: runs in vedit-render (and in tests). The file appears only when complete: the encoder writes a hidden
// temporary file next to it, renamed at the end.
class Renderer
{
public:
    enum class Status
    {
        Done,
        Cancelled,
        Failed,
    };

    struct Result
    {
        Status status = Status::Failed;
        RenderError error = RenderError::None;
        QString detail;       // technical, untranslated (MLT/FFmpeg message)
        QStringList warnings; // clips not rendered yet, missing media…
    };

    // Called about ten times per second with the frames written so far.
    using Progress = std::function<void(int frame, int total)>;

    static Result render(const ProjectData &project, const SequenceId &sequenceId, const ExportSettings &settings,
                         const Progress &progress, const std::atomic<bool> &cancel);
};

} // namespace vedit::engine
