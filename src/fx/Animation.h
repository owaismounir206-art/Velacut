// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QStringView>

namespace velacut::fx {

// Preset animations of clips (SPEC §5.6; manifest animations.json): how an entry, exit or loop animation changes the
// placement of a clip at a moment of it. The renderer (vedit.transform) and the pictures of the library use these same
// functions. `id` is the asset id ("animations/in/fade") or its last part; unknown names fade.
//  - entry: `t` 0…1 over the animation (already eased): 0 = start, 1 = the clip as placed;
//  - exit: `t` 0…1: 0 = the clip as placed, 1 = gone;
//  - loop: `cycle` 0…1 within one cycle.
// Positions are offsets in canvas widths/heights, scales factors, rotation degrees, opacity 0…1, crops fractions.
void applyInAnimation(QStringView id, double t, double &posX, double &posY, double &scX, double &scY, double &rot,
                      double &op, double &cL, double &cT, double &cR, double &cB);
void applyOutAnimation(QStringView id, double t, double &posX, double &posY, double &scX, double &scY, double &rot,
                       double &op, double &cL, double &cT, double &cR, double &cB);
void applyLoopAnimation(QStringView id, double cycleT, double &posX, double &posY, double &scX, double &scY, double &rot,
                        double &op);

} // namespace velacut::fx
