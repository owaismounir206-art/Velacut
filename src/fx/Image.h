// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace velacut::fx {

// Views on RGBA8888 images with straight (non-premultiplied) alpha, the format of the whole CPU pipeline
// (docs/ARCHITECTURE.md §6). `stride` is in bytes.
struct ImageView
{
    std::uint8_t *data = nullptr;
    int width = 0;
    int height = 0;
    int stride = 0;

    std::uint8_t *row(int y) const { return data + static_cast<long>(y) * stride; }
};

struct ConstImageView
{
    const std::uint8_t *data = nullptr;
    int width = 0;
    int height = 0;
    int stride = 0;

    ConstImageView() = default;
    ConstImageView(const std::uint8_t *d, int w, int h, int s)
        : data(d)
        , width(w)
        , height(h)
        , stride(s)
    {
    }
    ConstImageView(const ImageView &view) // NOLINT(google-explicit-constructor): read-only view of a writable one
        : data(view.data)
        , width(view.width)
        , height(view.height)
        , stride(view.stride)
    {
    }

    const std::uint8_t *row(int y) const { return data + static_cast<long>(y) * stride; }
};

} // namespace velacut::fx
