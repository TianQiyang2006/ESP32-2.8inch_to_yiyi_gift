#pragma once

// One LCD submission for each completed LVGL refresh. This policy deliberately
// has no LVGL/Arduino dependencies so the render/submit ordering is testable.
namespace DisplayFrame {

struct Rect { int x1, y1, x2, y2; };
enum class Action { None, Present, Defer };

class Damage {
public:
    void add(int x1, int y1, int x2, int y2) {
        if (x1 > x2 || y1 > y2 || x2 < 0 || y2 < 0 || x1 >= 320 || y1 >= 240) return;
        x1 = x1 < 0 ? 0 : x1; y1 = y1 < 0 ? 0 : y1;
        x2 = x2 > 319 ? 319 : x2; y2 = y2 > 239 ? 239 : y2;
        if (!count_) area_ = {x1, y1, x2, y2};
        else {
            area_.x1 = x1 < area_.x1 ? x1 : area_.x1;
            area_.y1 = y1 < area_.y1 ? y1 : area_.y1;
            area_.x2 = x2 > area_.x2 ? x2 : area_.x2;
            area_.y2 = y2 > area_.y2 ? y2 : area_.y2;
        }
        if (count_ < 0xffffu) ++count_;
    }

    bool take(Rect &out) {
        if (!count_) return false;
        out = area_; clear(); return true;
    }

    Action finish(bool last, bool capturing, bool transition, Rect &out) {
        // Rendering can touch several parts of the same digit. Never publish
        // any part until the renderer marks the whole refresh as complete.
        if (!last || !take(out)) return Action::None;
        if (capturing) return Action::None;
        return transition ? Action::Defer : Action::Present;
    }

    void clear() { count_ = 0; }
    unsigned regions() const { return count_; }
    bool empty() const { return count_ == 0; }

private:
    Rect area_ = {0, 0, -1, -1};
    unsigned count_ = 0;
};

} // namespace DisplayFrame
