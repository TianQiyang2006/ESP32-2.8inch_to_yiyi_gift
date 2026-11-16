// Compile the exact production renderer with only its external raster malloc
// exposed to host fault injection. Pre-include dependencies before the macro so
// no library declarations or other application allocations are substituted.
#include "Arduino.h"
#include "hardware/arduino/yiyi_desk/src/pages/mini_apps.h"
#include "hardware/arduino/yiyi_desk/src/app_state.h"
#include "hardware/arduino/yiyi_desk/src/clock_source.h"
#include "hardware/arduino/yiyi_desk/src/gift_data.h"
#include "hardware/arduino/yiyi_desk/src/touch.h"
#include "hardware/arduino/yiyi_desk/src/saved_design/saved_resources.h"
#include "hardware/arduino/yiyi_desk/src/gift_assets/gift_fonts.h"
#include "hardware/arduino/yiyi_desk/src/ui_widgets.h"
#include "hardware/arduino/yiyi_desk/src/text_util.h"
#include "hardware/arduino/yiyi_desk/desk_logic.h"
#include <stdio.h>
#include <stdlib.h>
bool HostFailFlipAllocation=false;
static void *hostFlipAlloc(size_t bytes){return HostFailFlipAllocation?nullptr:malloc(bytes);}
#define malloc hostFlipAlloc
#include "hardware/arduino/yiyi_desk/src/pages/flip_clock_page.cpp"
#undef malloc
