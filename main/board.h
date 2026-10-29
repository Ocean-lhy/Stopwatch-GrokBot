#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "i2c_bus.h"

// Initializes the StopWatch PMIC, IO expander, touch controller and buttons.
// The display rail is enabled before this function returns.
bool board_init(void);


// Returns true when a finger is down. Coordinates are controller-native.
bool board_touch_read(uint16_t *x, uint16_t *y);
