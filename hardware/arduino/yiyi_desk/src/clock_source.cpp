#include "clock_source.h"
#include <time.h>
namespace ClockSource {int64_t now(){return (int64_t)time(nullptr);}}
