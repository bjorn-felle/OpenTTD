//
// Created by bjorn on 20.05.25.
//

#ifndef BLITTER_HELPERS_H
#define BLITTER_HELPERS_H


#include "../gfx_type.h"


    /**
 * Scale a raw sprite buffer using nearest-neighbour algorithm.
 * @param src        Source pixel buffer (RGBA).
 * @param src_width  Width of the source sprite.
 * @param src_height Height of the source sprite.
 * @param zoom       Zoom factor (e.g. 1.5 = 150% size).
 * @param out_width  Output width (set by function).
 * @param out_height Output height (set by function).
 * @return Unique pointer to newly allocated scaled pixel buffer.
 */
std::optional<std::unique_ptr<Colour[]>> ScaleSpriteNearest(
    const Colour *src,
    int src_width,
    int src_height,
    float zoom,
    int &out_width,
    int &out_height
);

#endif //BLITTER_HELPERS_H
