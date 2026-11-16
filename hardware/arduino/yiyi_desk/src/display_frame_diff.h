#pragma once

#include <stdint.h>
#include "display_frame_damage.h"

// Pure pixel planning for the 320x240 RGB565 framebuffer. A plan describes only
// differing pixels with a small bounded amount of coalescing; publishing its
// rectangles from a stable completed frame
// reconstructs that frame in the panel mirror without any dynamic allocation.
namespace DisplayFrame {

struct DiffPlan {
    static const unsigned Capacity = 300; // 20 columns x 15 rows of 16px tiles.
    Rect patches[Capacity];
    unsigned count = 0;
    unsigned totalPixels = 0;
};

inline void planDiff(const uint16_t *frame, const uint16_t *presented,
                     const Rect &dirty, DiffPlan &out, int stride = 320) {
    out.count = 0;
    out.totalPixels = 0;
    if (!frame || !presented || frame == presented || stride < 320 ||
        dirty.x1 > dirty.x2 || dirty.y1 > dirty.y2 ||
        dirty.x2 < 0 || dirty.y2 < 0 || dirty.x1 >= 320 || dirty.y1 >= 240) return;

    const int left = dirty.x1 < 0 ? 0 : dirty.x1;
    const int top = dirty.y1 < 0 ? 0 : dirty.y1;
    const int right = dirty.x2 > 319 ? 319 : dirty.x2;
    const int bottom = dirty.y2 > 239 ? 239 : dirty.y2;

    // Rotation 1 maps the panel's native gate scan axis to logical x. Keep
    // patches column-major so the presentation layer can follow that order.
    for (int tileX = left / 16 * 16; tileX <= right; tileX += 16) {
        const int x1 = tileX < left ? left : tileX;
        const int x2 = tileX + 15 > right ? right : tileX + 15;
        for (int tileY = top / 16 * 16; tileY <= bottom; tileY += 16) {
            const int y1 = tileY < top ? top : tileY;
            const int y2 = tileY + 15 > bottom ? bottom : tileY + 15;
            Rect changed = {x2 + 1, y2 + 1, x1 - 1, y1 - 1};
            for (int y = y1; y <= y2; ++y) {
                const int offset = y * stride;
                for (int x = x1; x <= x2; ++x) {
                    // Compare every RGB565 bit, including faint AA edges.
                    if (frame[offset + x] == presented[offset + x]) continue;
                    if (x < changed.x1) changed.x1 = x;
                    if (x > changed.x2) changed.x2 = x;
                    if (y < changed.y1) changed.y1 = y;
                    if (y > changed.y2) changed.y2 = y;
                }
            }
            if (changed.x1 > changed.x2) continue;
            const unsigned changedPixels = unsigned(changed.x2 - changed.x1 + 1) *
                                           unsigned(changed.y2 - changed.y1 + 1);
            // Adjacent tiles often contain a continuous digit edge. Saving an
            // SPI address-window transaction is worth a few extra black pixels,
            // while a full-height column would waste bandwidth in sparse areas.
            // Keep the same scan column, at most 96 rows and <=16 extra pixels
            // per join. Coalesce in place: capacity and allocation stay bounded.
            if (out.count) {
                Rect &previous = out.patches[out.count - 1];
                if (previous.x1 / 16 == tileX / 16 &&
                    changed.y2 - previous.y1 + 1 <= 96) {
                    const Rect joined = {previous.x1 < changed.x1 ? previous.x1 : changed.x1,
                                         previous.y1,
                                         previous.x2 > changed.x2 ? previous.x2 : changed.x2,
                                         changed.y2};
                    const unsigned previousPixels = unsigned(previous.x2 - previous.x1 + 1) *
                                                    unsigned(previous.y2 - previous.y1 + 1);
                    const unsigned joinedPixels = unsigned(joined.x2 - joined.x1 + 1) *
                                                  unsigned(joined.y2 - joined.y1 + 1);
                    if (joinedPixels <= previousPixels + changedPixels + 16) {
                        previous = joined;
                        out.totalPixels += joinedPixels - previousPixels;
                        continue;
                    }
                }
            }
            // There can be at most one unmerged patch per screen tile (300).
            out.patches[out.count++] = changed;
            out.totalPixels += changedPixels;
        }
    }
}

} // namespace DisplayFrame
