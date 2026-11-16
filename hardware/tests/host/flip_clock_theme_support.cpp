// Reuse standard host hardware substitutes and expose the touch driver's
// physical-swipe result for this pointer suite. The production Touch driver
// suppresses clicks after >32px travel before LVGL dispatches release.
#define swiped hostNonSwipedStub
#include "support.cpp"
#undef swiped
bool HostTouchSwiped=false;
namespace Touch {bool swiped(){return HostTouchSwiped;}}
