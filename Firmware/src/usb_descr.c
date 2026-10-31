// ===================================================================================
// USB Descriptors
// Based on usb_descr.c by Stefan Wagner (github.com/wagiminator), CC BY-SA 3.0.
// ===================================================================================

#include "config.h"
#include "usb_descr.h"

__code USB_DEV_DESCR DevDescr = {
  .bLength            = sizeof(DevDescr),
  .bDescriptorType    = USB_DESCR_TYP_DEVICE,
  .bcdUSB             = 0x0110,
  .bDeviceClass       = 0,                      // defined per interface
  .bDeviceSubClass    = 0,
  .bDeviceProtocol    = 0,
  .bMaxPacketSize0    = EP0_SIZE,
  .idVendor           = USB_VENDOR_ID,
  .idProduct          = USB_PRODUCT_ID,
  .bcdDevice          = USB_DEVICE_VERSION,
  .iManufacturer      = 1,
  .iProduct           = 2,
  .iSerialNumber      = 3,
  .bNumConfigurations = 1
};

__code USB_CFG_DESCR_MINIKEYS CfgDescr = {
  .config = {
    .bLength            = sizeof(USB_CFG_DESCR),
    .bDescriptorType    = USB_DESCR_TYP_CONFIG,
    .wTotalLength       = sizeof(CfgDescr),
    .bNumInterfaces     = 2,
    .bConfigurationValue= 1,
    .iConfiguration     = 0,
    .bmAttributes       = 0x80,                   // bus powered
    .MaxPower           = USB_MAX_POWER_mA / 2
  },

  // Interface 0: HID keyboard + consumer + mouse
  .interface0 = {
    .bLength            = sizeof(USB_ITF_DESCR),
    .bDescriptorType    = USB_DESCR_TYP_INTERF,
    .bInterfaceNumber   = 0,
    .bAlternateSetting  = 0,
    .bNumEndpoints      = 1,
    .bInterfaceClass    = USB_DEV_CLASS_HID,
    .bInterfaceSubClass = 0,                      // no boot protocol (report IDs are used)
    .bInterfaceProtocol = 0,
    .iInterface         = 4
  },
  .hid0 = {
    .bLength            = sizeof(USB_HID_DESCR),
    .bDescriptorType    = USB_DESCR_TYP_HID,
    .bcdHID             = 0x0110,
    .bCountryCode       = 0,
    .bNumDescriptors    = 1,
    .bDescriptorTypeX   = 34,                     // report descriptor
    .wDescriptorLength  = sizeof(ReportDescr)
  },
  .ep1IN = {
    .bLength            = sizeof(USB_ENDP_DESCR),
    .bDescriptorType    = USB_DESCR_TYP_ENDP,
    .bEndpointAddress   = USB_ENDP_ADDR_EP1_IN,
    .bmAttributes       = USB_ENDP_TYPE_INTER,
    .wMaxPacketSize     = EP1_SIZE,
    .bInterval          = 1                       // 1 ms
  },

  // Interface 1: vendor-specific programming interface (same role as the stock firmware's)
  .interface1 = {
    .bLength            = sizeof(USB_ITF_DESCR),
    .bDescriptorType    = USB_DESCR_TYP_INTERF,
    .bInterfaceNumber   = 1,
    .bAlternateSetting  = 0,
    .bNumEndpoints      = 1,
    .bInterfaceClass    = USB_DEV_CLASS_VEN_SPEC,
    .bInterfaceSubClass = 0,
    .bInterfaceProtocol = 0,
    .iInterface         = 0
  },
  .ep2OUT = {
    .bLength            = sizeof(USB_ENDP_DESCR),
    .bDescriptorType    = USB_DESCR_TYP_ENDP,
    .bEndpointAddress   = USB_ENDP_ADDR_EP2_OUT,
    .bmAttributes       = USB_ENDP_TYPE_INTER,
    .wMaxPacketSize     = EP2_SIZE,
    .bInterval          = 1
  }
};

__code uint8_t ReportDescr[] = {
  // Keyboard: report 1 = modifiers, reserved, 6 keys
  0x05, 0x01, 0x09, 0x06, 0xa1, 0x01,
  0x85, 0x01,
  0x05, 0x07, 0x19, 0xe0, 0x29, 0xe7, 0x15, 0x00, 0x25, 0x01,
  0x95, 0x08, 0x75, 0x01, 0x81, 0x02,             // modifiers
  0x95, 0x01, 0x75, 0x08, 0x81, 0x03,             // reserved
  0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x26, 0xff, 0x00,
  0x05, 0x07, 0x19, 0x00, 0x29, 0xe7, 0x81, 0x00, // keys
  0xc0,

  // Consumer control: report 2 = one 16-bit usage
  0x05, 0x0c, 0x09, 0x01, 0xa1, 0x01,
  0x85, 0x02,
  0x15, 0x00, 0x26, 0xff, 0x03, 0x19, 0x00, 0x2a, 0xff, 0x03,
  0x95, 0x01, 0x75, 0x10, 0x81, 0x00,
  0xc0,

  // Mouse: report 3 = buttons, x, y, wheel
  0x05, 0x01, 0x09, 0x02, 0xa1, 0x01,
  0x85, 0x03,
  0x09, 0x01, 0xa1, 0x00,
  0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01,
  0x95, 0x03, 0x75, 0x01, 0x81, 0x02,             // 3 buttons
  0x95, 0x01, 0x75, 0x05, 0x81, 0x03,             // padding
  0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38,
  0x15, 0x81, 0x25, 0x7f, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06, // x, y, wheel (relative)
  0xc0,
  0xc0
};

__code uint8_t ReportDescrLen = sizeof(ReportDescr);

__code uint16_t LangDescr[] = {
  ((uint16_t)USB_DESCR_TYP_STRING << 8) | sizeof(LangDescr), 0x0409 };
__code uint16_t ManufDescr[] = {
  ((uint16_t)USB_DESCR_TYP_STRING << 8) | sizeof(ManufDescr), MANUFACTURER_STR };
__code uint16_t ProdDescr[] = {
  ((uint16_t)USB_DESCR_TYP_STRING << 8) | sizeof(ProdDescr), PRODUCT_STR };
__code uint16_t SerDescr[] = {
  ((uint16_t)USB_DESCR_TYP_STRING << 8) | sizeof(SerDescr), SERIAL_STR };
__code uint16_t InterfDescr[] = {
  ((uint16_t)USB_DESCR_TYP_STRING << 8) | sizeof(InterfDescr), INTERFACE_STR };
