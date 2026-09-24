// SPDX-License-Identifier: GPL-3.0-or-later
// QML registration of engine types (the engine itself stays free of QML).
#pragma once

#include "engine/playback/FrameSink.h"
#include "engine/playback/Player.h"

#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace vedit::ui {

struct PlayerForeign
{
    Q_GADGET
    QML_FOREIGN(vedit::engine::Player)
    QML_NAMED_ELEMENT(Player)
    QML_UNCREATABLE("Provided by App.player")
};

struct FrameSinkForeign
{
    Q_GADGET
    QML_FOREIGN(vedit::engine::FrameSink)
    QML_ANONYMOUS
};

} // namespace vedit::ui
