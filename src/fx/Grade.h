// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <array>
#include <optional>
#include <utility>
#include <vector>

namespace velacut::fx {

// Colour grading (SPEC §5.10): white balance gains, colour wheels, HSL per colour range and curves. Every value 0 (or
// an empty curve) changes nothing. Like the adjustments, a grade is a function of the colour of a pixel, so it is
// rendered through the 33³ LUT (ColorLut) together with them.
struct Grade
{
    // The 8 ranges of HSL, by hue: red, orange, yellow, green, aqua, blue, purple, magenta.
    static constexpr int kRanges = 8;
    struct HslRange
    {
        double hue = 0;        // −1…+1: ±30° towards the neighbouring colours
        double saturation = 0; // −1 = grey … +1 = twice as saturated
        double lightness = 0;  // −1…+1
        friend bool operator==(const HslRange &, const HslRange &) = default;
    };
    // A colour wheel: a colour pushed into shadows, midtones or highlights (an RGB offset, −1…+1 per channel, from the
    // position on the wheel) and the level of that zone.
    struct Wheel
    {
        std::array<double, 3> colour{0, 0, 0};
        double level = 0;
        friend bool operator==(const Wheel &, const Wheel &) = default;
    };
    using Curve = std::vector<std::pair<double, double>>; // (input, output) in 0…1, sorted by input

    std::array<double, 3> balance{1, 1, 1}; // per-channel gains (auto white balance, match colour)
    Wheel shadows;                          // lift
    Wheel midtones;                         // gamma
    Wheel highlights;                       // gain
    std::array<HslRange, kRanges> hsl{};
    Curve master;                           // then per channel
    std::array<Curve, 3> channel;

    bool isIdentity() const;
    friend bool operator==(const Grade &, const Grade &) = default;

    // The params of a "vedit.grade" effect (docs/EFFECT_FORMAT.md): "balance.r", "shadows.r" … "shadows.level",
    // "hsl.red.hue", "curve.master" ([[x, y], …]) … Missing = neutral.
    static Grade fromJson(const QJsonObject &params);
    QJsonObject toJson() const;
    static QString rangeName(int range); // "red" … "magenta"
};

// The grade applied to one RGB value 0…1.
std::array<double, 3> applyGrade(const Grade &grade, std::array<double, 3> rgb);

// A smooth curve through the points (monotone cubic: no overshoot between them), with (0,0) and (1,1) added when
// the points do not reach the ends.
double evaluateCurve(const Grade::Curve &curve, double x);

// A 3D (or 1D) LUT from a .cube file (Adobe/Resolve format): the colour transform of a look made elsewhere.
class CubeLut
{
public:
    static std::optional<CubeLut> parse(const QByteArray &text, QString *error = nullptr);
    static std::optional<CubeLut> load(const QString &path, QString *error = nullptr);

    int size() const { return m_size; }
    bool is3d() const { return m_3d; }
    QString title() const { return m_title; }
    // Trilinear (3D) or linear per channel (1D) sampling of an RGB value 0…1.
    std::array<double, 3> sample(std::array<double, 3> rgb) const;

private:
    int m_size = 0;
    bool m_3d = true;
    QString m_title;
    std::array<double, 3> m_min{0, 0, 0};
    std::array<double, 3> m_max{1, 1, 1};
    std::vector<std::array<float, 3>> m_table; // 3D: index r + g·N + b·N² (red fastest, as in the file)
};

} // namespace velacut::fx
