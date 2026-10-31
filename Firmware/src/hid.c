// ===================================================================================
// USB endpoints: HID reports (EP1 IN) and MiniKeys commands (EP2 OUT)
// Based on usb_hid.c by Stefan Wagner (github.com/wagiminator), CC BY-SA 3.0.
// ===================================================================================

#include "ch554.h"
#include "usb.h"
#include "usb_descr.h"
#include "usb_handler.h"
#include "hid.h"

volatile __bit HID_EP1_writeBusyFlag = 0;

// Command ring buffer, filled by the USB interrupt and drained by the main loop.
__xdata uint8_t CMD_queue[CMD_SLOTS][CMD_LEN];
volatile __idata uint8_t CMD_head = 0;      // next slot the interrupt writes
volatile __idata uint8_t CMD_tail = 0;      // next slot the main loop reads

void HID_init(void) {
  USB_init();
  UEP1_T_LEN = 0;
}

void HID_sendReport(__xdata uint8_t* buf, uint8_t len) {
  uint8_t i;
  while(HID_EP1_writeBusyFlag);                             // wait until the last report left
  for(i=0; i<len; i++) EP1_buffer[i] = buf[i];
  UEP1_T_LEN = len;
  HID_EP1_writeBusyFlag = 1;
  UEP1_CTRL = UEP1_CTRL & ~MASK_UEP_T_RES | UEP_T_RES_ACK;  // upload on the next IN token
}

uint8_t CMD_take(__xdata uint8_t* cmd) {
  uint8_t i;
  if(CMD_tail == CMD_head) return 0;
  for(i=0; i<CMD_LEN; i++) cmd[i] = CMD_queue[CMD_tail][i];
  CMD_tail = (CMD_tail + 1) & (CMD_SLOTS - 1);
  UEP2_CTRL = UEP2_CTRL & ~MASK_UEP_R_RES | UEP_R_RES_ACK;  // there is room again
  return 1;
}

// ===================================================================================
// USB handler callbacks
// ===================================================================================

void HID_setup(void) {
  UEP1_DMA    = EP1_ADDR;
  UEP2_DMA    = EP2_ADDR;
  UEP1_CTRL   = bUEP_AUTO_TOG | UEP_T_RES_NAK;
  UEP2_CTRL   = bUEP_AUTO_TOG | UEP_R_RES_ACK;
  UEP4_1_MOD  = bUEP1_TX_EN;
  UEP2_3_MOD  = bUEP2_RX_EN;
}

void HID_reset(void) {
  UEP1_CTRL = bUEP_AUTO_TOG | UEP_T_RES_NAK;
  UEP2_CTRL = bUEP_AUTO_TOG | UEP_R_RES_ACK;
  HID_EP1_writeBusyFlag = 0;
}

void HID_EP1_IN(void) {
  UEP1_T_LEN = 0;
  UEP1_CTRL = UEP1_CTRL & ~MASK_UEP_T_RES | UEP_T_RES_NAK;
  HID_EP1_writeBusyFlag = 0;
}

// MiniKeys sends 65-byte transfers: report ID 3 + command, split into a 64-byte and a 1-byte
// packet. Only packets starting with 3 carry a command; the trailing byte is ignored.
void CMD_EP2_OUT(void) {
  uint8_t i, next;
  if(U_TOG_OK && USB_RX_LEN > CMD_LEN && EP2_buffer[0] == 3) {
    next = (CMD_head + 1) & (CMD_SLOTS - 1);
    if(next != CMD_tail) {
      for(i=0; i<CMD_LEN; i++) CMD_queue[CMD_head][i] = EP2_buffer[i + 1];
      CMD_head = next;
    }
    // Queue full: NAK further packets until the main loop takes one.
    if(((CMD_head + 1) & (CMD_SLOTS - 1)) == CMD_tail)
      UEP2_CTRL = UEP2_CTRL & ~MASK_UEP_R_RES | UEP_R_RES_NAK;
  }
}
