// SPDX-License-Identifier: GPL-3.0-or-later
#include "Transition.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace vedit::fx {

namespace {

constexpr std::array kNames{
    std::pair{TransitionKind::Dissolve, std::string_view("dissolve")},
    std::pair{TransitionKind::DipToBlack, std::string_view("dipToBlack")},
    std::pair{TransitionKind::DipToWhite, std::string_view("dipToWhite")},
    std::pair{TransitionKind::SlideLeft, std::string_view("slideLeft")},
    std::pair{TransitionKind::SlideRight, std::string_view("slideRight")},
    std::pair{TransitionKind::SlideUp, std::string_view("slideUp")},
    std::pair{TransitionKind::SlideDown, std::string_view("slideDown")},
    std::pair{TransitionKind::PushLeft, std::string_view("pushLeft")},
    std::pair{TransitionKind::PushRight, std::string_view("pushRight")},
    std::pair{TransitionKind::PushUp, std::string_view("pushUp")},
    std::pair{TransitionKind::PushDown, std::string_view("pushDown")},
    std::pair{TransitionKind::WipeLeft, std::string_view("wipeLeft")},
    std::pair{TransitionKind::WipeRight, std::string_view("wipeRight")},
    std::pair{TransitionKind::WipeUp, std::string_view("wipeUp")},
    std::pair{TransitionKind::WipeDown, std::string_view("wipeDown")},
    std::pair{TransitionKind::Iris, std::string_view("iris")},
    std::pair{TransitionKind::Clock, std::string_view("clock")},
    std::pair{TransitionKind::ZoomIn, std::string_view("zoomIn")},
};

struct Pixel
{
    float r = 0, g = 0, b = 0, a = 0; // premultiplied, 0…1
};

inline Pixel load(ConstImageView image, int x, int y)
{
    if (x < 0 || y < 0 || x >= image.width || y >= image.height) {
        return {};
    }
    const std::uint8_t *p = image.row(y) + x * 4;
    const float a = p[3] / 255.0f;
    return {p[0] / 255.0f * a, p[1] / 255.0f * a, p[2] / 255.0f * a, a};
}

inline Pixel mix(Pixel p, Pixel q, float t)
{
    return {p.r + (q.r - p.r) * t, p.g + (q.g - p.g) * t, p.b + (q.b - p.b) * t, p.a + (q.a - p.a) * t};
}

inline void store(std::uint8_t *d, Pixel p)
{
    const auto byte = [](float v) { return static_cast<std::uint8_t>(std::clamp(std::lround(v * 255.0f), 0L, 255L)); };
    if (p.a <= 0.0f) {
        d[0] = d[1] = d[2] = d[3] = 0;
        return;
    }
    d[0] = byte(p.r / p.a);
    d[1] = byte(p.g / p.a);
    d[2] = byte(p.b / p.a);
    d[3] = byte(p.a);
}

inline float smooth(float edge0, float edge1, float x)
{
    if (edge1 <= edge0) {
        return x < edge0 ? 0.0f : 1.0f;
    }
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

// Bilinear sample (clamped to the edges) at pixel coordinates.
inline Pixel sample(ConstImageView image, float x, float y)
{
    x = std::clamp(x - 0.5f, 0.0f, float(image.width - 1));
    y = std::clamp(y - 0.5f, 0.0f, float(image.height - 1));
    const int x0 = static_cast<int>(x);
    const int y0 = static_cast<int>(y);
    const int x1 = std::min(x0 + 1, image.width - 1);
    const int y1 = std::min(y0 + 1, image.height - 1);
    const float tx = x - x0;
    const float ty = y - y0;
    return mix(mix(load(image, x0, y0), load(image, x1, y0), tx), mix(load(image, x0, y1), load(image, x1, y1), tx), ty);
}

} // namespace

std::optional<TransitionKind> transitionKindFromName(std::string_view name)
{
    for (const auto &[kind, text] : kNames) {
        if (text == name) {
            return kind;
        }
    }
    return std::nullopt;
}

std::string_view transitionKindName(TransitionKind kind)
{
    for (const auto &[value, text] : kNames) {
        if (value == kind) {
            return text;
        }
    }
    return {};
}

double ease(Easing easing, double t)
{
    t = std::clamp(t, 0.0, 1.0);
    switch (easing) {
    case Easing::Linear:
        return t;
    case Easing::EaseIn:
        return t * t * t;
    case Easing::EaseOut:
        return 1 - std::pow(1 - t, 3);
    case Easing::EaseInOut:
        return t < 0.5 ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2;
    }
    return t;
}

void renderTransition(TransitionKind kind, ImageView out, ConstImageView a, ConstImageView b, double progress,
                      const TransitionParams &params, int rowBegin, int rowEnd)
{
    const float t = static_cast<float>(std::clamp(progress, 0.0, 1.0));
    const int w = out.width;
    const int h = out.height;
    const float soft = static_cast<float>(std::max(0.0, params.softness));
    const Pixel black{0, 0, 0, 1};
    const Pixel white{1, 1, 1, 1};
    const float cx = w / 2.0f;
    const float cy = h / 2.0f;
    const float maxRadius = std::sqrt(cx * cx + cy * cy);
    for (int y = std::max(0, rowBegin); y < std::min(rowEnd, h); ++y) {
        std::uint8_t *d = out.row(y);
        for (int x = 0; x < w; ++x, d += 4) {
            Pixel p;
            switch (kind) {
            case TransitionKind::Dissolve:
                p = mix(load(a, x, y), load(b, x, y), t);
                break;
            case TransitionKind::DipToBlack:
            case TransitionKind::DipToWhite: {
                const Pixel colour = kind == TransitionKind::DipToBlack ? black : white;
                p = t < 0.5f ? mix(load(a, x, y), colour, t * 2) : mix(colour, load(b, x, y), (t - 0.5f) * 2);
                break;
            }
            case TransitionKind::SlideLeft:
            case TransitionKind::SlideRight:
            case TransitionKind::SlideUp:
            case TransitionKind::SlideDown:
            case TransitionKind::PushLeft:
            case TransitionKind::PushRight:
            case TransitionKind::PushUp:
            case TransitionKind::PushDown: {
                // Offset of B (entering from the opposite side) and, for push, of A.
                const bool horizontal = kind == TransitionKind::SlideLeft || kind == TransitionKind::SlideRight ||
                                        kind == TransitionKind::PushLeft || kind == TransitionKind::PushRight;
                const bool positive = kind == TransitionKind::SlideLeft || kind == TransitionKind::SlideUp ||
                                      kind == TransitionKind::PushLeft || kind == TransitionKind::PushUp;
                const bool push = kind >= TransitionKind::PushLeft && kind <= TransitionKind::PushDown;
                const float length = horizontal ? float(w) : float(h);
                const float bOffset = (positive ? 1.0f : -1.0f) * length * (1.0f - t); // B's origin
                const float aOffset = push ? bOffset - (positive ? length : -length) : 0.0f;
                const float coordinate = horizontal ? x : y;
                const float inB = coordinate - bOffset;
                if (inB >= 0 && inB < length) {
                    p = horizontal ? sample(b, x + 0.5f - bOffset, y + 0.5f) : sample(b, x + 0.5f, y + 0.5f - bOffset);
                    if (p.a < 1.0f) { // B has transparent parts: A (or nothing) shows through
                        const Pixel under = push ? Pixel{} : load(a, x, y);
                        p = {p.r + under.r * (1 - p.a), p.g + under.g * (1 - p.a), p.b + under.b * (1 - p.a),
                             p.a + under.a * (1 - p.a)};
                    }
                } else {
                    p = push ? (horizontal ? sample(a, x + 0.5f - aOffset, y + 0.5f) : sample(a, x + 0.5f, y + 0.5f - aOffset))
                             : load(a, x, y);
                }
                break;
            }
            case TransitionKind::WipeLeft:
            case TransitionKind::WipeRight:
            case TransitionKind::WipeUp:
            case TransitionKind::WipeDown: {
                // Position along the wipe direction 0…1 where 0 is where B appears first.
                float s = 0;
                switch (kind) {
                case TransitionKind::WipeLeft: s = 1.0f - (x + 0.5f) / w; break;
                case TransitionKind::WipeRight: s = (x + 0.5f) / w; break;
                case TransitionKind::WipeUp: s = 1.0f - (y + 0.5f) / h; break;
                default: s = (y + 0.5f) / h; break;
                }
                // The edge travels from −soft to 1 + soft so that t = 0 is all A and t = 1 all B.
                const float edge = t * (1.0f + 2 * soft) - soft;
                p = mix(load(a, x, y), load(b, x, y), 1.0f - smooth(edge - soft, edge + soft, s));
                break;
            }
            case TransitionKind::Iris: {
                const float r = std::sqrt((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy)) / maxRadius;
                const float edge = t * (1.0f + 2 * soft) - soft;
                p = mix(load(a, x, y), load(b, x, y), 1.0f - smooth(edge - soft, edge + soft, r));
                break;
            }
            case TransitionKind::Clock: {
                // Angle from 12 o'clock, clockwise, 0…1.
                float angle = std::atan2(x + 0.5f - cx, cy - (y + 0.5f)) / (2 * float(M_PI));
                if (angle < 0) {
                    angle += 1.0f;
                }
                const float edge = t * (1.0f + 2 * soft) - soft;
                p = mix(load(a, x, y), load(b, x, y), 1.0f - smooth(edge - soft, edge + soft, angle));
                break;
            }
            case TransitionKind::ZoomIn: {
                const float zoom = 1.0f + t;
                const Pixel zoomed = sample(a, cx + (x + 0.5f - cx) / zoom, cy + (y + 0.5f - cy) / zoom);
                p = mix(zoomed, load(b, x, y), smooth(0.2f, 1.0f, t));
                break;
            }
            }
            store(d, p);
        }
    }
}

} // namespace vedit::fx
