// SPDX-License-Identifier: GPL-3.0-or-later
#include "Animation.h"

#include <algorithm>
#include <cmath>

namespace vedit::fx {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kPi2 = 1.57079632679489661923;

} // namespace

void applyInAnimation(QStringView id, double t, double &posX, double &posY, double &scX, double &scY, double &rot,
                      double &op, double &cL, double &cT, double &cR, double &cB)
{
    QStringView name = id;
    if (name.startsWith(u"animations/in/")) {
        name = name.mid(14);
    }
    if (name == u"fade") {
        op *= t;
    } else if (name == u"zoom_in") {
        scX *= t;
        scY *= t;
        op *= std::clamp(t * 1.5, 0.0, 1.0);
    } else if (name == u"zoom_out") {
        const double s = 2.0 - t;
        scX *= s;
        scY *= s;
        op *= t;
    } else if (name == u"slide_left") {
        posX += (1.0 - t);
    } else if (name == u"slide_right") {
        posX -= (1.0 - t);
    } else if (name == u"slide_up") {
        posY += (1.0 - t);
    } else if (name == u"slide_down") {
        posY -= (1.0 - t);
    } else if (name == u"spin") {
        rot += (1.0 - t) * 360.0;
        op *= t;
    } else if (name == u"bounce") {
        const double b = std::abs(std::sin(t * kPi * 2.5)) * (1.0 - t);
        posY += b;
        scY *= (1.0 + b * 0.2);
    } else if (name == u"wipe_right") {
        cR = std::max(cR, 1.0 - t);
    } else if (name == u"wipe_left") {
        cL = std::max(cL, 1.0 - t);
    } else if (name == u"wipe_up") {
        cB = std::max(cB, 1.0 - t);
    } else if (name == u"wipe_down") {
        cT = std::max(cT, 1.0 - t);
    } else if (name == u"rotate_cw") {
        rot += (1.0 - t) * 180.0;
        scX *= t;
        scY *= t;
    } else if (name == u"rotate_ccw") {
        rot -= (1.0 - t) * 180.0;
        scX *= t;
        scY *= t;
    } else if (name == u"pop") {
        const double s = (t < 0.7) ? (t / 0.7 * 1.2) : (1.2 - 0.2 * (t - 0.7) / 0.3);
        scX *= s;
        scY *= s;
    } else if (name == u"drop") {
        posY -= (1.0 - t) * 1.5;
        op *= t;
    } else if (name == u"rise") {
        posY += (1.0 - t) * 1.5;
        op *= t;
    } else if (name == u"swing") {
        rot += std::sin((1.0 - t) * kPi * 2.0) * 30.0;
        op *= t;
    } else if (name == u"flip_x") {
        scY *= std::cos((1.0 - t) * kPi2);
    } else if (name == u"flip_y") {
        scX *= std::cos((1.0 - t) * kPi2);
    } else if (name == u"roll_left") {
        posX += (1.0 - t);
        rot += (1.0 - t) * 360.0;
    } else if (name == u"roll_right") {
        posX -= (1.0 - t);
        rot -= (1.0 - t) * 360.0;
    } else if (name == u"expand_h") {
        scX *= t;
    } else if (name == u"expand_v") {
        scY *= t;
    } else if (name == u"elastic_in") {
        const double s = std::sin(t * kPi * 4.5) * (1.0 - t) * 0.3 + t;
        scX *= s;
        scY *= s;
        op *= t;
    } else if (name == u"back_in") {
        const double s = t * t * (2.70158 * t - 1.70158);
        scX *= s;
        scY *= s;
        op *= t;
    } else if (name == u"fade_slide_up") {
        posY += (1.0 - t) * 0.5;
        op *= t;
    } else if (name == u"fade_slide_down") {
        posY -= (1.0 - t) * 0.5;
        op *= t;
    } else if (name == u"fade_zoom") {
        scX *= (0.5 + 0.5 * t);
        scY *= (0.5 + 0.5 * t);
        op *= t;
    } else {
        op *= t;
    }
}

void applyOutAnimation(QStringView id, double t, double &posX, double &posY, double &scX, double &scY, double &rot,
                       double &op, double &cL, double &cT, double &cR, double &cB)
{
    QStringView name = id;
    if (name.startsWith(u"animations/out/")) {
        name = name.mid(15);
    }
    if (name == u"fade") {
        op *= (1.0 - t);
    } else if (name == u"zoom_in") {
        const double s = 1.0 + t;
        scX *= s;
        scY *= s;
        op *= (1.0 - t);
    } else if (name == u"zoom_out" || name == u"shrink") {
        scX *= (1.0 - t);
        scY *= (1.0 - t);
        op *= (1.0 - t);
    } else if (name == u"slide_left") {
        posX -= t;
    } else if (name == u"slide_right") {
        posX += t;
    } else if (name == u"slide_up") {
        posY -= t;
    } else if (name == u"slide_down") {
        posY += t;
    } else if (name == u"spin") {
        rot -= t * 360.0;
        op *= (1.0 - t);
    } else if (name == u"bounce") {
        const double b = std::abs(std::sin(t * kPi * 2.5)) * t;
        posY += b;
        scY *= (1.0 + b * 0.2);
        op *= (1.0 - t);
    } else if (name == u"wipe_right") {
        cL = std::max(cL, t);
    } else if (name == u"wipe_left") {
        cR = std::max(cR, t);
    } else if (name == u"wipe_up") {
        cT = std::max(cT, t);
    } else if (name == u"wipe_down") {
        cB = std::max(cB, t);
    } else if (name == u"rotate_cw") {
        rot += t * 180.0;
        scX *= (1.0 - t);
        scY *= (1.0 - t);
    } else if (name == u"rotate_ccw") {
        rot -= t * 180.0;
        scX *= (1.0 - t);
        scY *= (1.0 - t);
    } else if (name == u"fall") {
        posY += t * 1.5;
        op *= (1.0 - t);
    } else if (name == u"sink") {
        posY -= t * 1.5;
        op *= (1.0 - t);
    } else if (name == u"swing") {
        rot += std::sin(t * kPi * 2.0) * 30.0;
        op *= (1.0 - t);
    } else if (name == u"flip_x") {
        scY *= std::cos(t * kPi2);
    } else if (name == u"flip_y") {
        scX *= std::cos(t * kPi2);
    } else if (name == u"roll_left") {
        posX -= t;
        rot -= t * 360.0;
    } else if (name == u"roll_right") {
        posX += t;
        rot += t * 360.0;
    } else if (name == u"contract_h") {
        scX *= (1.0 - t);
    } else if (name == u"contract_v") {
        scY *= (1.0 - t);
    } else if (name == u"elastic_out") {
        const double s = (1.0 - t) + std::sin(t * kPi * 4.5) * t * 0.3;
        scX *= std::max(0.0, s);
        scY *= std::max(0.0, s);
        op *= (1.0 - t);
    } else if (name == u"back_out") {
        const double inv = 1.0 - t;
        const double s = inv * inv * (2.70158 * inv - 1.70158);
        scX *= s;
        scY *= s;
        op *= (1.0 - t);
    } else if (name == u"fade_slide_up") {
        posY -= t * 0.5;
        op *= (1.0 - t);
    } else if (name == u"fade_slide_down") {
        posY += t * 0.5;
        op *= (1.0 - t);
    } else if (name == u"fade_zoom") {
        scX *= (1.0 - 0.5 * t);
        scY *= (1.0 - 0.5 * t);
        op *= (1.0 - t);
    } else {
        op *= (1.0 - t);
    }
}

void applyLoopAnimation(QStringView id, double cycleT, double &posX, double &posY, double &scX, double &scY,
                        double &rot, double &op)
{
    QStringView name = id;
    if (name.startsWith(u"animations/loop/")) {
        name = name.mid(16);
    }
    const double angle = cycleT * 2.0 * kPi;
    if (name == u"pulse") {
        const double factor = 1.0 + 0.15 * std::sin(angle);
        scX *= factor;
        scY *= factor;
    } else if (name == u"wobble" || name == u"shake") {
        rot += 6.0 * std::sin(angle);
    } else if (name == u"float") {
        posY += 0.04 * std::sin(angle);
    } else if (name == u"blink" || name == u"flash") {
        if (std::sin(angle) < 0.0) {
            op *= 0.2;
        }
    } else if (name == u"rotate" || name == u"spin_slow") {
        rot += cycleT * 360.0;
    } else if (name == u"bounce") {
        posY += std::abs(std::sin(angle)) * 0.08;
    } else if (name == u"swing" || name == u"pendulum") {
        rot += 12.0 * std::sin(angle);
    } else if (name == u"heartbeat") {
        const double hb = std::pow(std::max(0.0, std::sin(angle)), 4.0) * 0.2;
        scX *= (1.0 + hb);
        scY *= (1.0 + hb);
    } else if (name == u"jiggle" || name == u"vibrate") {
        posX += 0.02 * std::sin(angle * 2.0);
        posY += 0.02 * std::cos(angle * 3.0);
    } else if (name == u"breathe") {
        const double br = 1.0 + 0.08 * std::sin(angle);
        scX *= br;
        scY *= br;
    } else if (name == u"zoom_pulse") {
        const double zp = 1.0 + 0.2 * std::sin(angle);
        scX *= zp;
        scY *= zp;
    } else if (name == u"sway") {
        posX += 0.05 * std::sin(angle);
        rot += 3.0 * std::sin(angle);
    } else if (name == u"wave") {
        posY += 0.04 * std::sin(angle);
        rot += 4.0 * std::cos(angle);
    } else if (name == u"tilt") {
        rot += 8.0 * std::sin(angle);
    } else if (name == u"flicker") {
        op *= (0.7 + 0.3 * std::sin(angle * 3.0));
    } else if (name == u"rock") {
        rot += 10.0 * std::sin(angle);
    } else if (name == u"shiver") {
        posX += 0.015 * std::sin(angle * 4.0);
    } else if (name == u"bob") {
        posY += 0.05 * std::cos(angle);
    } else if (name == u"drift") {
        posX += 0.03 * std::sin(angle);
        posY += 0.03 * std::cos(angle);
    } else if (name == u"twist") {
        rot += 15.0 * std::sin(angle);
    } else if (name == u"orbit") {
        posX += 0.03 * std::cos(angle);
        posY += 0.03 * std::sin(angle);
    } else if (name == u"zigzag") {
        posX += (cycleT < 0.5 ? (cycleT * 4.0 - 1.0) : (3.0 - cycleT * 4.0)) * 0.04;
    } else if (name == u"heart_pulse") {
        double hp = std::sin(angle);
        if (hp > 0.0) {
            hp = std::sqrt(hp);
        }
        scX *= (1.0 + 0.15 * hp);
        scY *= (1.0 + 0.15 * hp);
    } else if (name == u"tremble") {
        posX += 0.01 * std::sin(angle * 5.0);
        posY += 0.01 * std::cos(angle * 5.0);
    } else if (name == u"hover") {
        posY += 0.03 * std::sin(angle);
        scX *= (1.0 + 0.03 * std::cos(angle));
    }
}

} // namespace vedit::fx
