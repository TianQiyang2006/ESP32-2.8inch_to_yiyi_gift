#include "src/display_frame_diff.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

using DisplayFrame::DiffPlan;
using DisplayFrame::Rect;
static unsigned checks = 0, scenarios = 0;
static uint32_t randomState = 0x4dc028a1;
static uint32_t randomNext() {
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;
    return randomState;
}
static void check(bool value) { ++checks; assert(value); }
static void verify(const std::vector<uint16_t> &before,
                   const std::vector<uint16_t> &after, Rect dirty,
                   int stride = 320, int expectedCount = -1) {
    DiffPlan plan;
    DisplayFrame::planDiff(after.data(), before.data(), dirty, plan, stride);
    check(plan.count <= DiffPlan::Capacity);
    if (expectedCount >= 0) check(plan.count == unsigned(expectedCount));
    std::vector<uint16_t> panel = before;
    std::vector<unsigned char> visits(320 * 240, 0);
    unsigned written = 0;
    int previousTile = -1;
    for (unsigned n = 0; n < plan.count; ++n) {
        const Rect &p = plan.patches[n];
        check(p.x1 >= 0 && p.y1 >= 0 && p.x2 < 320 && p.y2 < 240);
        check(p.x1 <= p.x2 && p.y1 <= p.y2);
        check(p.x1 >= dirty.x1 && p.y1 >= dirty.y1 && p.x2 <= dirty.x2 && p.y2 <= dirty.y2);
        check(p.x1 / 16 == p.x2 / 16 && p.y2 - p.y1 + 1 <= 96);
        const int tile = p.x1 / 16 * 240 + p.y1;
        check(tile > previousTile); previousTile = tile;
        bool left = false, right = false, top = false, bottom = false;
        for (int y = p.y1; y <= p.y2; ++y) for (int x = p.x1; x <= p.x2; ++x) {
            check(visits[y * 320 + x]++ == 0);
            panel[y * stride + x] = after[y * stride + x];
            ++written;
            if (before[y * stride + x] != after[y * stride + x]) {
                left = left || x == p.x1; right = right || x == p.x2;
                top = top || y == p.y1; bottom = bottom || y == p.y2;
            }
        }
        // Every edge contains a real changed pixel: the patch is minimal.
        check(left && right && top && bottom);
    }
    check(written == plan.totalPixels);
    check(panel == after); // End-to-end oracle: exactly reconstruct the new LCD.
    ++scenarios;
}

int main() {
    std::vector<uint16_t> old(320 * 240, 0), next = old;
    const Rect screen = {0, 0, 319, 239};
    verify(old, next, screen, 320, 0);

    // One-bit antialias differences, all corners, and both sides of tile edges.
    for (int y : {0, 15, 16, 31, 32, 223, 224, 239})
        for (int x : {0, 15, 16, 31, 32, 303, 304, 319}) {
            next = old; next[y * 320 + x] = 1;
            verify(old, next, screen, 320, 1);
        }
    for (int y = 0; y < 240; ++y) for (int x = 0; x < 320; ++x)
        next[y * 320 + x] = uint16_t(1 + ((x + y) % 65535));
    verify(old, next, screen, 320, 60);
    verify(old, next, {-1000, -2000, 4000, 5000}, 320, 60);

    // A continuous full-height edge becomes three bounded 96-row patches.
    next = old;
    for (int y = 0; y < 240; ++y) next[y * 320 + 5] = 1;
    verify(old, next, screen, 320, 3);
    DiffPlan merged;
    DisplayFrame::planDiff(next.data(), old.data(), screen, merged);
    check(merged.totalPixels == 240);

    // A cheap join may include a few unchanged pixels, but larger empty
    // rectangles remain split. This pins bandwidth rather than just counts.
    next = old;
    for (int y = 14; y <= 17; ++y) for (int x = 10; x <= (y < 16 ? 11 : 12); ++x)
        next[y * 320 + x] = 1;
    verify(old, next, screen, 320, 1);
    DisplayFrame::planDiff(next.data(), old.data(), screen, merged);
    check(merged.totalPixels == 12); // Ten changed pixels plus two for the join.
    next = old;
    for (int y = 0; y < 32; ++y) next[y * 320 + (y < 16 ? 0 : 1)] = 1;
    verify(old, next, screen, 320, 2); // Joining would add 32 unchanged pixels.

    // Sparse glyph-like outlines make tile payload far smaller than a full band.
    next = old;
    for (int y = 65; y <= 185; ++y) for (int x = 13; x <= 306; ++x) {
        const int dx = (x % 50) - 25, dy = y - 125;
        const int oval = dx * dx * 7 + dy * dy;
        if (oval > 3500 && oval < 3800) next[y * 320 + x] = uint16_t((x + y) & 1 ? 1 : 0xffff);
    }
    verify(old, next, {13, 65, 306, 185});
    DiffPlan contour;
    DisplayFrame::planDiff(next.data(), old.data(), {13, 65, 306, 185}, contour);
    check(contour.totalPixels < 294u * 121u / 2u);

    // Arbitrary old frames and dirty rectangles, including clipped screen edges.
    for (unsigned trial = 0; trial < 240; ++trial) {
        for (auto &pixel : old) pixel = uint16_t(randomNext());
        next = old;
        int x1 = int(randomNext() % 400) - 40, x2 = int(randomNext() % 400) - 40;
        int y1 = int(randomNext() % 320) - 40, y2 = int(randomNext() % 320) - 40;
        if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
        if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
        for (int y = 0; y < 240; ++y) for (int x = 0; x < 320; ++x)
            if (x >= x1 && x <= x2 && y >= y1 && y <= y2 &&
                (trial % 4 == 0 || randomNext() % 13 == 0))
                next[y * 320 + x] ^= uint16_t(1u << (randomNext() % 16));
        verify(old, next, {x1, y1, x2, y2});
    }

    // A larger row stride cannot leak padding bytes into a screen patch.
    old.assign(336 * 240, 0xaaaa); next = old;
    for (int y = 0; y < 240; ++y) next[y * 336 + 319] ^= 1;
    verify(old, next, screen, 336, 3);

    DiffPlan empty;
    DisplayFrame::planDiff(next.data(), next.data(), screen, empty, 336);
    check(empty.count == 0 && empty.totalPixels == 0);
    DisplayFrame::planDiff(nullptr, old.data(), screen, empty);
    check(empty.count == 0 && empty.totalPixels == 0);
    DisplayFrame::planDiff(next.data(), nullptr, screen, empty);
    check(empty.count == 0 && empty.totalPixels == 0);
    DisplayFrame::planDiff(next.data(), old.data(), screen, empty, 319);
    check(empty.count == 0 && empty.totalPixels == 0);
    for (Rect bad : {Rect{20, 10, 19, 20}, Rect{0, 5, 10, 4}, Rect{320, 0, 400, 240},
                     Rect{0, 240, 319, 500}, Rect{-30, -20, -1, 100}, Rect{0, -20, 319, -1}}) {
        DisplayFrame::planDiff(next.data(), old.data(), bad, empty, 336);
        check(empty.count == 0 && empty.totalPixels == 0);
    }
    std::printf("{\"passed\":true,\"scenarios\":%u,\"checks\":%u,\"patch_capacity\":%u,\"contour_pixels\":%u,\"contour_full_band_pixels\":35574}\n",
                scenarios, checks, DiffPlan::Capacity, contour.totalPixels);
}
