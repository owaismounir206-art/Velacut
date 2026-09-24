// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/commands/Edit.h"
#include "core/project/Sequence.h"

namespace vedit {

// Computes the minimal EditScript that turns `before` into `after` (same sequence id).
// Supports: tracks inserted/removed (surviving tracks keep their relative order), clips inserted/removed/
// modified (clips that only moved by the same offset become a single shift edit), transitions,
// canvas, markers and groups. Used by TimelineEditor, which simulates each operation on a copy.
EditScript diffSequence(const Sequence &before, const Sequence &after);

} // namespace vedit
