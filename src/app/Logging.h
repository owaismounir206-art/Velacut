// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

namespace vedit::logging {

// Log file in ~/.local/state/vedit/logs/vedit.log (rotated at 5 MB, one previous file kept), plus stderr.
// Installed before anything else logs; also receives the MLT and FFmpeg messages forwarded by the engine.
void install();
QString logFilePath();

} // namespace vedit::logging
