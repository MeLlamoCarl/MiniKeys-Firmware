// ===================================================================================
// USB Descriptors: HID keyboard/media/mouse (interface 0) and the vendor-specific
// programming interface (interface 1), which macOS leaves unclaimed.
// Based on usb_descr.h by Stefan Wagner (github.com/wagiminator), CC BY-SA 3.0.
// ===================================================================================

#pragma once
#include <stdint.h>
#include "usb.h"

// ===================================================================================
// USB Endpoint Addresses and Sizes
// ===================================================================================
#define EP0_SIZE        64
#define EP1_SIZE        16              // HID reports to the host
#define EP2_SIZE        64              // commands from MiniKeys
#define EP3_SIZE        8               // events to MiniKeys (layer switched on the keypad)

#define EP0_ADDR        0
#define EP1_ADDR        (EP0_ADDR + EP0_BUF_SIZE)
#define EP2_ADDR        (EP1_ADDR + EP1_BUF_SIZE)
#define EP3_ADDR        (EP2_ADDR + EP2_BUF_SIZE)

#define EP0_BUF_SIZE    EP_BUF_SIZE(EP0_SIZE)
#define EP1_BUF_SIZE    EP_BUF_SIZE(EP1_SIZE)
#define EP2_BUF_SIZE    EP_BUF_SIZE(EP2_SIZE)
#define EP3_BUF_SIZE    EP_BUF_SIZE(EP3_SIZE)

#define EP_BUF_SIZE(x)  (x+2<64 ? x+2 : 64)

// ===================================================================================
// Device and Configuration Descriptors
// ===================================================================================
typedef struct _USB_CFG_DESCR_MINIKEYS {
  USB_CFG_DESCR config;
  USB_ITF_DESCR interface0;
  USB_HID_DESCR hid0;
  USB_ENDP_DESCR ep1IN;
  USB_ITF_DESCR interface1;
  USB_ENDP_DESCR ep2OUT;
  USB_ENDP_DESCR ep3IN;
} USB_CFG_DESCR_MINIKEYS;

extern __code USB_DEV_DESCR DevDescr;
extern __code USB_CFG_DESCR_MINIKEYS CfgDescr;

// ===================================================================================
// HID Report Descriptor
// ===================================================================================
extern __code uint8_t ReportDescr[];
extern __code uint8_t ReportDescrLen;

#define USB_REPORT_DESCR      ReportDescr
#define USB_REPORT_DESCR_LEN  ReportDescrLen

// ===================================================================================
// String Descriptors
// ===================================================================================
extern __code uint16_t LangDescr[];
extern __code uint16_t ManufDescr[];
extern __code uint16_t ProdDescr[];
extern __code uint16_t SerDescr[];
extern __code uint16_t InterfDescr[];

#define USB_STR_DESCR_i0    (uint8_t*)LangDescr
#define USB_STR_DESCR_i1    (uint8_t*)ManufDescr
#define USB_STR_DESCR_i2    (uint8_t*)ProdDescr
#define USB_STR_DESCR_i3    (uint8_t*)SerDescr
#define USB_STR_DESCR_i4    (uint8_t*)InterfDescr
#define USB_STR_DESCR_ix    (uint8_t*)SerDescr
