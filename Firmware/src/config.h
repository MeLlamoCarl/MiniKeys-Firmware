// ===================================================================================
// MiniKeys firmware configuration: CH552G 3-key + knob keypad (USB 1189:8890)
// ===================================================================================

#pragma once

// Pin definitions (traced by biemster, hackaday.io/project/189914)
#define PIN_KEY1            P11         // left key
#define PIN_KEY2            P17         // middle key
#define PIN_KEY3            P16         // right key
#define PIN_ENC_A           P30         // knob encoder A
#define PIN_ENC_B           P31         // knob encoder B
#define PIN_ENC_SW          P33         // knob push switch
#define PIN_NEO             P34         // WS2812 data, 3 pixels (one per key)

// NeoPixel configuration
#define NEO_GRB                         // type of pixel: NEO_GRB or NEO_RGB
#define NEO_COUNT           3

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
