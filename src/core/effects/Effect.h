// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/effects/Param.h"
#include "core/project/Id.h"

#include <QString>

#include <map>
#include <optional>

namespace vedit {

// Reference to an item of an asset library (transitions, effects, filters, text presets, stickers…).
// docs/FILE_FORMAT.md §5.9. Kept intact even when the pack is not installed.
struct AssetRef
{
    QString pack;
    QString id;
    int version = 1;

    bool isValid() const { return !pack.isEmpty() && !id.isEmpty() && version >= 1; }
    friend bool operator==(const AssetRef &, const AssetRef &) = default;
};

// An effect applied to a clip or placed on an effect/adjustment track (docs/FILE_FORMAT.md §5.6).
// Parameters are stored by name without interpretation: the effect registry (src/fx) knows their types,
// the core only stores, animates and round-trips them. Unknown effect types are preserved as they are.
struct Effect
{
    EffectId id;
    QString type;
    int typeVersion = 1;
    bool enabled = true;
    std::optional<AssetRef> preset;
    Param intensity{1.0};
    std::map<QString, Param> params; // ordered: stable serialization

    friend bool operator==(const Effect &, const Effect &) = default;
};

} // namespace vedit
