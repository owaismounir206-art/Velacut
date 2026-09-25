// SPDX-License-Identifier: GPL-3.0-or-later
#include "Composite.h"

#include <algorithm>

namespace vedit::fx {

namespace {
// Rounded integer division for non-negative values.
inline unsigned divRound(unsigned numerator, unsigned denominator)
{
    return (numerator + denominator / 2) / denominator;
}
} // namespace

void compositeOver(ImageView destination, ConstImageView source, int opacity, int rowBegin, int rowEnd)
{
    const int width = std::min(destination.width, source.width);
    const int endRow = std::min({rowEnd, destination.height, source.height});
    const unsigned op = static_cast<unsigned>(std::clamp(opacity, 0, 255));
    if (op == 0) {
        return;
    }
    for (int y = std::max(0, rowBegin); y < endRow; ++y) {
        std::uint8_t *d = destination.row(y);
        const std::uint8_t *s = source.row(y);
        for (int x = 0; x < width; ++x, d += 4, s += 4) {
            const unsigned sa = op == 255 ? s[3] : divRound(s[3] * op, 255);
            if (sa == 0) {
                continue;
            }
            if (sa == 255) {
                d[0] = s[0];
                d[1] = s[1];
                d[2] = s[2];
                d[3] = 255;
                continue;
            }
            const unsigned da = d[3];
            // outA = sa + da (1 - sa), everything scaled by 255.
            const unsigned inverse = 255 - sa;
            const unsigned outA255 = sa * 255 + da * inverse; // outA * 255
            if (outA255 == 0) {
                continue;
            }
            for (int c = 0; c < 3; ++c) {
                // Straight alpha: outC = (sC * sa + dC * da * (1 - sa)) / outA
                const unsigned numerator = s[c] * sa * 255 + d[c] * da * inverse;
                d[c] = static_cast<std::uint8_t>(std::min(255u, divRound(numerator, outA255)));
            }
            d[3] = static_cast<std::uint8_t>(divRound(outA255, 255));
        }
    }
}

} // namespace vedit::fx
