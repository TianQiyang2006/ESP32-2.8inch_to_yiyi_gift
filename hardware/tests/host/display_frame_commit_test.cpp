#include "hardware/arduino/yiyi_desk/src/display_frame_damage.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <vector>

using DisplayFrame::Action;
using DisplayFrame::Rect;
static unsigned checks = 0;
static void check(bool value) { ++checks; assert(value); }
static bool equal(const Rect &a, const Rect &b) {
    return a.x1 == b.x1 && a.y1 == b.y1 && a.x2 == b.x2 && a.y2 == b.y2;
}

// A fake LCD observes the exact instant commits happen. The back buffer is
// progressively rendered while the panel must retain the previous generation.
struct Pipeline {
    DisplayFrame::Damage damage;
    std::vector<unsigned> back = std::vector<unsigned>(320 * 240, 7);
    std::vector<unsigned> panel = back;
    std::vector<Rect> commits, deferred;

    void paint(const Rect &area, unsigned color, bool last,
               bool capture = false, bool transition = false) {
        for (int y = std::max(0, area.y1); y <= std::min(239, area.y2); ++y)
            for (int x = std::max(0, area.x1); x <= std::min(319, area.x2); ++x)
                back[y * 320 + x] = color;
        damage.add(area.x1, area.y1, area.x2, area.y2);
        Rect update{};
        const Action action = damage.finish(last, capture, transition, update);
        if (action == Action::Defer) deferred.push_back(update);
        if (action != Action::Present) return;
        commits.push_back(update);
        for (int y = update.y1; y <= update.y2; ++y)
            for (int x = update.x1; x <= update.x2; ++x)
                panel[y * 320 + x] = back[y * 320 + x];
    }
    bool panelUniform(unsigned color) const {
        return std::all_of(panel.begin(), panel.end(), [=](unsigned p) { return p == color; });
    }
};

int main() {
    Pipeline p;
    // One changing glyph is split into upper and lower render regions. The
    // physical display must never expose the half-updated digit between them.
    p.paint({20, 40, 79, 89}, 9, false);
    check(p.panelUniform(7)); check(p.commits.empty());
    p.paint({20, 90, 79, 139}, 9, true);
    check(p.commits.size() == 1);
    check(equal(p.commits.back(), {20, 40, 79, 139}));
    for (int y = 40; y <= 139; ++y)
        for (int x = 20; x <= 79; ++x) check(p.panel[y * 320 + x] == 9);
    check(p.damage.empty());

    // Reverse-order, duplicate and overlapping regions are still one submit;
    // stable pixels between disjoint regions retain their back-buffer contents.
    const unsigned gap = p.panel[160 * 320 + 150];
    p.paint({250, 160, 270, 180}, 11, false);
    p.paint({250, 160, 270, 180}, 11, false);
    p.paint({15, 160, 35, 180}, 11, false);
    check(p.commits.size() == 1); check(p.damage.regions() == 3);
    p.paint({25, 170, 45, 190}, 11, true);
    check(p.commits.size() == 2);
    check(equal(p.commits.back(), {15, 160, 270, 190}));
    check(p.panel[160 * 320 + 150] == gap);

    // Captured destination construction updates the mirror without modifying
    // the LCD. Its discarded damage must not leak into the next normal frame.
    const auto beforeCapture = p.panel;
    p.paint({0, 0, 319, 119}, 13, false, true);
    check(p.panel == beforeCapture);
    p.paint({0, 120, 319, 239}, 13, true, true);
    check(p.panel == beforeCapture); check(p.damage.empty());
    check(p.commits.size() == 2 && p.deferred.empty());
    p.paint({100, 100, 104, 104}, 17, true);
    check(equal(p.commits.back(), {100, 100, 104, 104}));

    // A reveal owns the LCD: the refresh hands it one complete destination
    // region only after all pieces are ready, with no direct panel write.
    const auto beforeTransition = p.panel;
    p.paint({0, 30, 319, 80}, 19, false, false, true);
    check(p.deferred.empty()); check(p.panel == beforeTransition);
    p.paint({0, 81, 319, 130}, 19, true, false, true);
    check(p.deferred.size() == 1); check(p.panel == beforeTransition);
    check(equal(p.deferred.back(), {0, 30, 319, 130}));
    check(p.damage.empty());

    DisplayFrame::Damage damage;
    Rect out{1, 2, 3, 4};
    check(!damage.take(out)); check(equal(out, {1, 2, 3, 4}));
    check(damage.finish(true, false, false, out) == Action::None);
    damage.add(-50, -50, 5, 8); damage.add(310, 230, 400, 500);
    damage.add(50, 50, 40, 60); damage.add(1, 1, 5, -1);
    damage.add(320, 0, 400, 239); damage.add(-5, -5, -1, -1);
    check(damage.regions() == 2);
    check(damage.take(out)); check(equal(out, {0, 0, 319, 239}));
    check(damage.empty()); check(!damage.take(out));
    damage.add(20, 20, 30, 30); damage.clear();
    check(damage.finish(true, false, false, out) == Action::None);
    // Capturing wins if lifecycle changes ever make both suppression flags set.
    damage.add(0, 0, 319, 239);
    check(damage.finish(true, true, true, out) == Action::None);
    check(damage.empty());
    std::printf("PASS: %u checks; complete-frame submission, split glyph, overlap, capture, transition, clipping, reset.\n", checks);
}
