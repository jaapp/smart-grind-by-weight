#pragma once

// LVGL's desktop CMake support looks for lv_conf.h beside this CMake project
// during a clean configure. Keep the firmware configuration authoritative and
// only forward to it here; SMART_GRIND_SIM selects the Windows-specific paths.
#include "../include/lv_conf.h"
