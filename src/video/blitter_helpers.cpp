//
// Created by bjorn on 20.05.25.
//

#include "blitter_helpers.h"

std::optional<std::unique_ptr<Colour[]>> ScaleSpriteNearest(const Colour *src, int width, int height, float scale_factor, int &out_width, int &out_height)
{
    if (scale_factor == 1.0f) {
        out_width = width;
        out_height = height;
        return std::nullopt;
    }

    out_width = std::lround(width * scale_factor);
    out_height = std::lround(height * scale_factor);

    if (out_width == 0 || out_height == 0) return std::nullopt;

    auto scaled = std::make_unique<Colour[]>(out_width * out_height);

    for (int y = 0; y < out_height; ++y) {
        int src_y = std::min(static_cast<int>(y / scale_factor), height - 1);
        for (int x = 0; x < out_width; ++x) {
            int src_x = std::min(static_cast<int>(x / scale_factor), width - 1);
            scaled[y * out_width + x] = src[src_y * width + src_x];
        }
    }

    return scaled;
}
