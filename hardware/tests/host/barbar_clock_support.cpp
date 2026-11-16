// Share the production host substitutes, exposing only physical swipe travel.
#define swiped hostNonSwipedStub
#include "support.cpp"
#undef swiped
bool HostTouchSwiped=false;
namespace Touch {bool swiped(){return HostTouchSwiped;}}
