// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

namespace vedit::fonts {

// Registers the fonts that ship with vedit (Inter, regular and italic) once per process, and makes the family name
// used by projects and packs ("Inter") resolve to them: the editor, its previews and vedit-render then draw texts with
// the same font on every system. Needs a QGuiApplication. Returns the family name of Inter as registered ("Inter
// Variable"), empty if it could not be loaded.
QString loadBundled();

} // namespace vedit::fonts
