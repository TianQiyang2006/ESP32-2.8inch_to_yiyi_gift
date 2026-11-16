#pragma once
// Reference implementation of the Screens::Provider contract, and the template an
// AiDoGUI / SquareLine export follows.
//
// It deliberately behaves like generated designer code rather than like the
// built-in UI: it creates its own lv_obj_t screen once and makes it current with
// lv_scr_load(), instead of drawing into the firmware's shared root screen.
//
// It is disabled by default and only claims the Weather app slot, so it stays a
// regression test for the seam rather than a change in behaviour. Toggle it from
// the serial console with `uiexport on` / `uiexport off`.
#include "../screens.h"

namespace UiExport {

// Screens::Provider implementation.
bool provider(int slot);

void setEnabled(bool enabled);
bool enabled();

// Registers or unregisters the provider on the Screens seam.
void attach();
void detach();

// Verifies screen construction and that only the claimed slot is taken over.
bool selfTest();

}
