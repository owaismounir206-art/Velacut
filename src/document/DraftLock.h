// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

namespace velacut::document {

// The "lock" file of an open draft (docs/FILE_FORMAT.md §9.3): {pid, hostname, bootId, program, openedAt}.
// A lock whose process no longer exists (or from an earlier boot) means the previous session ended abnormally.
class DraftLock
{
public:
    enum class Outcome
    {
        Acquired,
        Recovered,     // acquired after an abnormal end of the previous session
        HeldElsewhere, // open in another running velacut
        Failed,        // the lock could not be written (see error)
    };

    static Outcome acquire(const QString &directory, QString *error);
    static void release(const QString &directory);
    // True if a running process holds the lock (e.g. the draft is open in another window).
    static bool isHeld(const QString &directory);
};

} // namespace velacut::document
