// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace Mlt {
class Repository;
}

namespace vedit::engine {

// Registers vedit's own MLT services (docs/ARCHITECTURE.md §5.2), in every process that renders:
//  - transition "vedit.composite": track compositing, `b` over `a` with the CPU reference kernel of vedit_fx
//    (straight alpha; blank frames of a track are skipped, so gaps never paint black).
// Called by MltRuntime right after Mlt::Factory::init().
void registerServices(Mlt::Repository *repository);

} // namespace vedit::engine
