// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace Mlt {
class Repository;
}

namespace vedit::engine {

// Owns the MLT factory. Initialization (loading every MLT module, ~100–300 ms) runs in a background
// thread started at application startup, off the critical path (docs/ARCHITECTURE.md §5.3).
// Probe finding: every MLT object must be destroyed before shutdown(), which unloads the modules.
class MltRuntime
{
public:
    // Idempotent; returns immediately.
    static void initializeAsync();
    // Blocks until initialized (starting it if needed). Returns false if MLT could not be initialized.
    static bool waitUntilReady();
    static bool isReady();
    static Mlt::Repository *repository();
    // Call at exit, after every MLT object has been destroyed.
    static void shutdown();
};

} // namespace vedit::engine
