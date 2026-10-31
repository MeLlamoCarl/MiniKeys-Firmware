// ===================================================================================
// USB endpoints: HID reports (EP1 IN) and MiniKeys commands (EP2 OUT)
// ===================================================================================

#pragma once
#include <stdint.h>

#define CMD_SLOTS   8                       // queued commands (power of two)
#define CMD_LEN     12                      // bytes kept per command

void HID_init(void);
void HID_sendReport(__xdata uint8_t* buf, uint8_t len);

// Copies the oldest queued command to `cmd` and returns 1, or returns 0 if none is waiting.
uint8_t CMD_take(__xdata uint8_t* cmd);
