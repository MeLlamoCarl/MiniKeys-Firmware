// ===================================================================================
// MiniKeys firmware configuration: CH552G 3-key or 6-key + knob keypad (USB 1189:8890)
// ===================================================================================

#pragma once

// Number of keys: 3 (1x3 board, default) or 6 (2x3 board). Build the 6-key image with
// `make KEYS=6`.
#ifndef KEYS
#define KEYS                3
#endif

// Pin definitions. Keys 1-3 and the knob as traced by biemster (hackaday.io/project/189914) on
// the 3-key board. The 2x3 board uses the same pins for its top row and knob; its bottom row
// was found with the DIAG=1 pin finder.
#define PIN_KEY1            P11         // left key (6-key: top row, left)
#define PIN_KEY2            P17         // middle key (6-key: top row, middle)
#define PIN_KEY3            P16         // right key (6-key: top row, right)
#if KEYS == 6
#define PIN_KEY4            P15         // bottom row, left
#define PIN_KEY5            P14         // bottom row, middle
#define PIN_KEY6            P32         // bottom row, right
#elif KEYS != 3
#error "KEYS must be 3 or 6"
#endif
#define PIN_ENC_A           P30         // knob encoder A
#define PIN_ENC_B           P31         // knob encoder B
#define PIN_ENC_SW          P33         // knob push switch
#define PIN_NEO             P34         // WS2812 data, one pixel per key

// NeoPixel configuration
#define NEO_GRB                         // type of pixel: NEO_GRB or NEO_RGB
#define NEO_COUNT           KEYS

// USB device descriptor: same IDs as the stock firmware, so the keypad keeps working with
// MiniKeys; bcdDevice and the product string tell MiniKeys this firmware is installed.
#define USB_VENDOR_ID       0x1189
#define USB_PRODUCT_ID      0x8890
#define USB_DEVICE_VERSION  0x2006      // 0x20xx = MiniKeys firmware, xx = version

#define USB_MAX_POWER_mA    100

#define MANUFACTURER_STR    'M','i','n','i','K','e','y','s'
#define PRODUCT_STR         'M','i','n','i','K','e','y','s',' ','K','e','y','p','a','d'
#define SERIAL_STR          'M','K','0','1'
#define INTERFACE_STR       'M','i','n','i','K','e','y','s'
