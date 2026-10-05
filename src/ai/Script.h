// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QStringList>

namespace vedit::ai {

// "Script to video" (SPEC §5.13bis): the scenes of a script — its paragraphs, a long paragraph split at the ends of its
// sentences into scenes of at most `maxWords` words. Empty lines and spaces do not make scenes.
QStringList splitScript(const QString &script, int maxWords = 30);

// How long a text takes to read aloud or on screen, at a calm pace (about 2.6 words a second, at least 2 s).
double readingSeconds(const QString &text);

} // namespace vedit::ai
