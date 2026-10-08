// SPDX-License-Identifier: GPL-3.0-or-later
// QML registration of engine types (the engine itself stays free of QML).
#pragma once

#include "engine/playback/FrameSink.h"
#include "engine/playback/TimelinePlayer.h"
#include "engine/render/RenderJob.h"

#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace velacut::ui {

struct TimelinePlayerForeign
{
    Q_GADGET
    QML_FOREIGN(velacut::engine::TimelinePlayer)
    QML_NAMED_ELEMENT(TimelinePlayer)
    QML_UNCREATABLE("Provided by App.editor.player")
};

struct RenderJobForeign
{
    Q_GADGET
    QML_FOREIGN(velacut::engine::RenderJob)
    QML_NAMED_ELEMENT(RenderJob)
    QML_UNCREATABLE("Provided by App.editor.exportJob")
};

struct FrameSinkForeign
{
    Q_GADGET
    QML_FOREIGN(velacut::engine::FrameSink)
    QML_ANONYMOUS
};

} // namespace velacut::ui
