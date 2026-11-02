// ===================================================================================
// MiniKeys firmware for the CH552G 3-key + knob keypad (USB 1189:8890)
// ===================================================================================
//
// Replaces the stock firmware while speaking the same programming protocol (see
// PROTOCOL.md), so MiniKeys keeps working, and adds full RGB control of the three WS2812
// LEDs: solid, breathing, rainbow, wave, on-press and layer colors, plus live per-key
// colors set by the app.
//
// - Hold key 1 while plugging in to enter the bootloader (all LEDs white).
// - Command BB 4D 4B also enters the bootloader, for updates without opening the keypad.
//
// Built on the CH55x libraries by Stefan Wagner (github.com/wagiminator), CC BY-SA 3.0.

#include "src/config.h"
#include "src/system.h"
#include "src/delay.h"
#include "src/gpio.h"
#include "src/neo.h"
#include "src/eeprom.h"
#include "src/hid.h"

void USB_interrupt(void);
void USB_ISR(void) __interrupt(INT_NO_USB) {
  USB_interrupt();
}

// Millisecond clock from timer 2, so effects keep time however long the main loop takes.
volatile __idata uint32_t msTicks = 0;

void TMR2_ISR(void) __interrupt(INT_NO_TMR2) {
  TF2 = 0;
  msTicks++;
}

uint32_t millis(void) {
  uint32_t now;
  ET2 = 0;
  now = msTicks;
  ET2 = 1;
  return now;
}

void TIMER_init(void) {
  T2MOD = T2MOD & ~bTMR_CLK | bT2_CLK;      // timer 2 at Fsys/4
  RCAP2 = 65536 - F_CPU / 4 / 1000;         // overflow every millisecond
  T2COUNT = RCAP2;
  T2CON = 0;                                // timer with auto reload
  ET2 = 1;
  TR2 = 1;
}

#pragma disable_warning 110                 // conditional flow changed by optimizer

// ===================================================================================
// Bindings
// ===================================================================================
// Controls: 0-2 = keys 1-3, 3 = knob left, 4 = knob press, 5 = knob right.
// Each slot: [type << 5 | payload length] + payload.
//   keys:  pairs of (modifiers, HID usage), up to 5 (length 0 = key disabled)
//   media: 16-bit Consumer usage, little endian
//   mouse: buttons, x, y, wheel, modifiers

#define LAYERS          4                   // 3 saved + 1 in RAM for layers MiniKeys loads live
#define SAVED_LAYERS    3
#define CONTROLS        6
#define SLOT_LEN        11
#define TYPE_NONE       0
#define TYPE_KEYS       1
#define TYPE_MEDIA      2
#define TYPE_MOUSE      3

#define CTRL_KNOB_LEFT  3
#define CTRL_KNOB_PRESS 4
#define CTRL_KNOB_RIGHT 5

__xdata uint8_t bindings[LAYERS * CONTROLS][SLOT_LEN];
__idata uint8_t activeLayer = 0;
__idata uint8_t editLayer = 0;

// ===================================================================================
// Light settings
// ===================================================================================

#define FX_OFF          0
#define FX_PRESS        1                   // pressed key lights up
#define FX_WAVE         2                   // light runs over keys 1 -> 2 -> 3
#define FX_SOLID        3
#define FX_BREATHE      4
#define FX_RAINBOW      5
#define FX_LAYER        6                   // solid in the active layer's color
#define FX_COUNT        7

#define FLAG_MULTICOLOR 0x01                // cycle colors instead of using the set color
#define FLAG_LAYERFLASH 0x02                // blink the layer number on layer changes

// light.reserved: bit 0 = press and turn the knob to switch saved layers. Bit 7 marks the byte as
// set (older settings: on).
#define COMBO_SET       0x80
#define COMBO_DEFAULT   0x01

typedef struct {
  uint8_t effect, r, g, b, speed, brightness, flags, reserved;
} LightSettings;

__xdata LightSettings light;
__xdata uint8_t layerColors[LAYERS][3];
__xdata uint8_t overrideColors[NEO_COUNT][3];
__idata uint8_t overrideMask = 0;            // bit i: pixel i shows overrideColors[i]

// LED index for each key, in case the pixel chain runs in another order.
__code uint8_t keyPixel[3] = {0, 1, 2};

// ===================================================================================
// Data flash layout (128 bytes)
// ===================================================================================
// 0: magic 'M', 1: layout version, 2-9: LightSettings, 10-18: layer colors,
// 19-127: bindings, packed (header byte + payload per slot).

#define FLASH_MAGIC     0x4D
#define FLASH_VERSION   1
#define FLASH_LIGHT     2
#define FLASH_LAYERS    10
#define FLASH_BINDINGS  19
#define FLASH_SIZE      128

void setDefaults(void) {
  uint8_t i;
  for(i=0; i<LAYERS * CONTROLS; i++) bindings[i][0] = 0;
  for(i=0; i<SAVED_LAYERS; i++) {
    // Keys 1, 2, 3 and the knob as volume, on every layer.
    __xdata uint8_t* s = bindings[i * CONTROLS];
    s[0] = TYPE_KEYS << 5 | 2; s[1] = 0; s[2] = 0x1E;       // 1
    s = bindings[i * CONTROLS + 1];
    s[0] = TYPE_KEYS << 5 | 2; s[1] = 0; s[2] = 0x1F;       // 2
    s = bindings[i * CONTROLS + 2];
    s[0] = TYPE_KEYS << 5 | 2; s[1] = 0; s[2] = 0x20;       // 3
    s = bindings[i * CONTROLS + CTRL_KNOB_LEFT];
    s[0] = TYPE_MEDIA << 5 | 2; s[1] = 0xEA; s[2] = 0;      // volume down
    s = bindings[i * CONTROLS + CTRL_KNOB_PRESS];
    s[0] = TYPE_MEDIA << 5 | 2; s[1] = 0xE2; s[2] = 0;      // mute
    s = bindings[i * CONTROLS + CTRL_KNOB_RIGHT];
    s[0] = TYPE_MEDIA << 5 | 2; s[1] = 0xE9; s[2] = 0;      // volume up
  }
  light.effect = FX_WAVE; light.r = 255; light.g = 255; light.b = 255;
  light.speed = 5; light.brightness = 160; light.flags = FLAG_MULTICOLOR | FLAG_LAYERFLASH;
  light.reserved = COMBO_SET | COMBO_DEFAULT;
  layerColors[0][0] = 0;   layerColors[0][1] = 120; layerColors[0][2] = 255;  // blue
  layerColors[1][0] = 0;   layerColors[1][1] = 255; layerColors[1][2] = 80;   // green
  layerColors[2][0] = 255; layerColors[2][1] = 60;  layerColors[2][2] = 0;    // orange
  layerColors[3][0] = 255; layerColors[3][1] = 255; layerColors[3][2] = 255;  // white
}

void loadSettings(void) {
  uint8_t i, j, addr, header, len;
  setDefaults();
  if(EEPROM_read(0) != FLASH_MAGIC || EEPROM_read(1) != FLASH_VERSION) return;
  for(i=0; i<8; i++) ((__xdata uint8_t*)&light)[i] = EEPROM_read(FLASH_LIGHT + i);
  for(i=0; i<SAVED_LAYERS; i++)
    for(j=0; j<3; j++) layerColors[i][j] = EEPROM_read(FLASH_LAYERS + i * 3 + j);
  if(light.effect >= FX_COUNT) light.effect = FX_WAVE;
  if(!light.speed || light.speed > 10) light.speed = 5;
  if(!(light.reserved & COMBO_SET)) light.reserved = COMBO_SET | COMBO_DEFAULT;
  else if(light.reserved & 0x07) light.reserved = COMBO_SET | COMBO_DEFAULT;   // firmware 4 combos
  addr = FLASH_BINDINGS;
  for(i=0; i<SAVED_LAYERS * CONTROLS; i++) {
    header = EEPROM_read(addr++);
    len = header & 0x1F;
    if(len >= SLOT_LEN || addr + len > FLASH_SIZE) { bindings[i][0] = 0; continue; }
    bindings[i][0] = header;
    for(j=0; j<len; j++) bindings[i][1 + j] = EEPROM_read(addr++);
  }
}

// Writes only the bytes that changed, to spare the flash. Returns 0 if the bindings don't fit.
uint8_t saveSettings(void) {
  uint8_t i, j, addr, len, total = FLASH_BINDINGS;
  for(i=0; i<SAVED_LAYERS * CONTROLS; i++) total += 1 + (bindings[i][0] & 0x1F);
  if(total > FLASH_SIZE) return 0;
  EEPROM_update(0, FLASH_MAGIC);
  EEPROM_update(1, FLASH_VERSION);
  for(i=0; i<8; i++) EEPROM_update(FLASH_LIGHT + i, ((__xdata uint8_t*)&light)[i]);
  for(i=0; i<SAVED_LAYERS; i++)
    for(j=0; j<3; j++) EEPROM_update(FLASH_LAYERS + i * 3 + j, layerColors[i][j]);
  addr = FLASH_BINDINGS;
  for(i=0; i<SAVED_LAYERS * CONTROLS; i++) {
    len = bindings[i][0] & 0x1F;
    for(j=0; j<=len; j++) EEPROM_update(addr++, bindings[i][j]);
  }
  return 1;
}

// ===================================================================================
// HID output
// ===================================================================================

__xdata uint8_t kbdReport[9];               // id 1, modifiers, reserved, 6 keys
__xdata uint8_t conReport[3];               // id 2, usage
__xdata uint8_t mouseReport[5];             // id 3, buttons, x, y, wheel
__xdata uint8_t heldMods[CONTROLS];
__xdata uint8_t heldKeys[CONTROLS];
__xdata uint8_t heldButtons[CONTROLS];
__idata uint8_t heldMediaCtrl = 0xFF;

// Sends the keyboard report for everything held, plus one extra (modifiers, key).
void sendKeys(uint8_t extraMods, uint8_t extraKey) {
  uint8_t i, n = 3;
  kbdReport[0] = 1;
  kbdReport[1] = extraMods;
  kbdReport[2] = 0;
  for(i=3; i<9; i++) kbdReport[i] = 0;
  if(extraKey) kbdReport[n++] = extraKey;
  for(i=0; i<CONTROLS; i++) {
    kbdReport[1] |= heldMods[i];
    if(heldKeys[i] && n < 9) kbdReport[n++] = heldKeys[i];
  }
  HID_sendReport(kbdReport, sizeof(kbdReport));
}

void sendMedia(uint16_t usage) {
  conReport[0] = 2;
  conReport[1] = usage & 0xFF;
  conReport[2] = usage >> 8;
  HID_sendReport(conReport, sizeof(conReport));
}

void sendMouse(int8_t x, int8_t y, int8_t wheel) {
  uint8_t i;
  mouseReport[0] = 3;
  mouseReport[1] = 0;
  for(i=0; i<CONTROLS; i++) mouseReport[1] |= heldButtons[i];
  mouseReport[2] = x;
  mouseReport[3] = y;
  mouseReport[4] = wheel;
  HID_sendReport(mouseReport, sizeof(mouseReport));
}

void controlDown(uint8_t ctrl) {
  __xdata uint8_t* s = bindings[activeLayer * CONTROLS + ctrl];
  uint8_t type = s[0] >> 5, len = s[0] & 0x1F, i, last;
  switch(type) {
    case TYPE_KEYS:
      if(len < 2) return;
      last = len - 2;
      // A sequence types every key but the last, which stays held while the control is.
      for(i=0; i<last; i+=2) {
        sendKeys(s[1 + i], s[2 + i]);
        sendKeys(0, 0);
      }
      heldMods[ctrl] = s[1 + last];
      heldKeys[ctrl] = s[2 + last];
      sendKeys(0, 0);
      break;
    case TYPE_MEDIA:
      heldMediaCtrl = ctrl;
      sendMedia(s[1] | (uint16_t)s[2] << 8);
      break;
    case TYPE_MOUSE:
      heldButtons[ctrl] = s[1];
      sendMouse(s[2], s[3], s[4]);
      if(s[2] || s[3] || s[4]) sendMouse(0, 0, 0);
      break;
  }
}

void controlUp(uint8_t ctrl) {
  if(heldMods[ctrl] || heldKeys[ctrl]) {
    heldMods[ctrl] = 0;
    heldKeys[ctrl] = 0;
    sendKeys(0, 0);
  }
  if(heldMediaCtrl == ctrl) {
    heldMediaCtrl = 0xFF;
    sendMedia(0);
  }
  if(heldButtons[ctrl]) {
    heldButtons[ctrl] = 0;
    sendMouse(0, 0, 0);
  }
}

void releaseAll(void) {
  uint8_t i;
  for(i=0; i<CONTROLS; i++) controlUp(i);
}

// ===================================================================================
// Lights
// ===================================================================================

// Effects are computed from the time since `effectStart`, which resets whenever MiniKeys sends
// light settings (B1) or asks to sync (B4); MiniKeys' preview uses the same formulas from the same
// moment, so both show the same frame.
__idata uint32_t effectStart = 0;
__idata uint32_t effectTime = 0;            // ms since effectStart, for this frame

// WS2812 LEDs respond linearly while the eye doesn't: without this curve (gamma 2.6) mixed colors
// wash out and half brightness looks like full brightness.
__code uint8_t gamma8[256] = {
    0,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
    1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
    1,   1,   1,   1,   2,   2,   2,   2,   2,   2,   2,   2,   3,   3,   3,   3,
    3,   3,   4,   4,   4,   4,   5,   5,   5,   5,   5,   6,   6,   6,   6,   7,
    7,   7,   8,   8,   8,   9,   9,   9,  10,  10,  10,  11,  11,  11,  12,  12,
   13,  13,  13,  14,  14,  15,  15,  16,  16,  17,  17,  18,  18,  19,  19,  20,
   20,  21,  21,  22,  22,  23,  24,  24,  25,  25,  26,  27,  27,  28,  29,  29,
   30,  31,  31,  32,  33,  34,  34,  35,  36,  37,  38,  38,  39,  40,  41,  42,
   42,  43,  44,  45,  46,  47,  48,  49,  50,  51,  52,  53,  54,  55,  56,  57,
   58,  59,  60,  61,  62,  63,  64,  65,  66,  68,  69,  70,  71,  72,  73,  75,
   76,  77,  78,  80,  81,  82,  84,  85,  86,  88,  89,  90,  92,  93,  94,  96,
   97,  99, 100, 102, 103, 105, 106, 108, 109, 111, 112, 114, 115, 117, 119, 120,
  122, 124, 125, 127, 129, 130, 132, 134, 136, 137, 139, 141, 143, 145, 146, 148,
  150, 152, 154, 156, 158, 160, 162, 164, 166, 168, 170, 172, 174, 176, 178, 180,
  182, 184, 186, 188, 191, 193, 195, 197, 199, 202, 204, 206, 209, 211, 213, 215,
  218, 220, 223, 225, 227, 230, 232, 235, 237, 240, 242, 245, 247, 250, 252, 255
};

// Per speed 1...10 (ms): breath length, rainbow turn, wave time per key.
__code uint16_t breathePeriod[10] = {6000, 5467, 4933, 4400, 3867, 3333, 2800, 2267, 1733, 1200};
__code uint16_t rainbowPeriod[10] = {20000, 18000, 16000, 14000, 12000, 10000, 8000, 6000, 4000, 2000};
__code uint16_t waveStep[10] = {400, 360, 340, 300, 260, 240, 200, 160, 140, 100};

// Position in a repeating period, scaled to 0...255.
uint8_t periodPhase(uint16_t period) {
  return (uint8_t)((effectTime % period) * 256 / period);
}
__xdata uint8_t pixels[NEO_COUNT][3];
__xdata uint8_t pressLevel[NEO_COUNT];
__xdata uint8_t pressColor[NEO_COUNT][3];
__idata uint8_t pressCount = 0;
__idata uint8_t layerFlashFrames = 0;
__idata uint8_t errorFlashFrames = 0;

// Colors filmed on the stock firmware: red, blue, green (wave) and red, green, purple, blue
// (on press).
__code uint8_t palette[4][3] = {{255, 20, 30}, {30, 80, 255}, {30, 255, 80}, {150, 60, 255}};

void setPixel(uint8_t i, uint8_t r, uint8_t g, uint8_t b) {
  pixels[i][0] = r; pixels[i][1] = g; pixels[i][2] = b;
}

void setPixelScaled(uint8_t i, __xdata uint8_t* color, uint8_t level) {
  setPixel(i, ((uint16_t)color[0] * level) >> 8, ((uint16_t)color[1] * level) >> 8,
           ((uint16_t)color[2] * level) >> 8);
}

void setPixelHue(uint8_t i, uint8_t hue) {
  uint8_t region = hue / 43, rise = (hue - region * 43) * 6, fall = 255 - rise;
  switch(region) {
    case 0:  setPixel(i, 255, rise, 0); break;
    case 1:  setPixel(i, fall, 255, 0); break;
    case 2:  setPixel(i, 0, 255, rise); break;
    case 3:  setPixel(i, 0, fall, 255); break;
    case 4:  setPixel(i, rise, 0, 255); break;
    default: setPixel(i, 255, 0, fall); break;
  }
}

__xdata uint8_t tmpColor[3];

__xdata uint8_t* lightColor(void) {
  tmpColor[0] = light.r; tmpColor[1] = light.g; tmpColor[2] = light.b;
  return tmpColor;
}

void flashPress(uint8_t key) {
  uint8_t i, pixel = keyPixel[key];
  pressLevel[pixel] = 255;
  for(i=0; i<3; i++)
    pressColor[pixel][i] = (light.flags & FLAG_MULTICOLOR) ? palette[pressCount & 3][i]
                                                           : ((__xdata uint8_t*)&light.r)[i];
  pressCount++;
}

void renderLights(void) {
  uint8_t i, level, phase, cycle;
  uint32_t position;
  __xdata uint8_t* color = lightColor();

  for(i=0; i<NEO_COUNT; i++) setPixel(i, 0, 0, 0);

  switch(light.effect) {
    case FX_SOLID:
      for(i=0; i<NEO_COUNT; i++) setPixelScaled(i, color, 255);
      break;

    case FX_BREATHE:
      phase = periodPhase(breathePeriod[light.speed - 1]);
      level = phase < 128 ? phase * 2 : (255 - phase) * 2;
      if(level < 12) level = 12;            // never quite off
      for(i=0; i<NEO_COUNT; i++) setPixelScaled(i, color, level);
      break;

    case FX_RAINBOW:
      phase = periodPhase(rainbowPeriod[light.speed - 1]);
      for(i=0; i<NEO_COUNT; i++) setPixelHue(i, phase + i * 64);
      break;

    case FX_WAVE:
      position = effectTime / waveStep[light.speed - 1];
      cycle = (position / 6) % 3;           // 3 keys lit, then 3 steps dark, next color
      phase = position % 6;
      if(phase < 3) {
        if(light.flags & FLAG_MULTICOLOR) {
          for(i=0; i<3; i++) tmpColor[i] = palette[cycle][i];
        }
        setPixelScaled(keyPixel[phase], tmpColor, 255);
      }
      break;

    case FX_LAYER:
      setPixelScaled(0, layerColors[activeLayer], 255);
      for(i=1; i<NEO_COUNT; i++) setPixelScaled(i, layerColors[activeLayer], 255);
      break;
  }

  // Key presses light up on top of every effect except off and wave.
  for(i=0; i<NEO_COUNT; i++) {
    if(pressLevel[i] && light.effect != FX_OFF && light.effect != FX_WAVE)
      setPixelScaled(i, pressColor[i], pressLevel[i]);
    level = 4 + light.speed * 3;
    pressLevel[i] = pressLevel[i] > level ? pressLevel[i] - level : 0;
  }

  // Layer number: one blink per layer in the layer's color.
  if(layerFlashFrames) {
    layerFlashFrames--;
    if(layerFlashFrames % 15 >= 6)
      for(i=0; i<NEO_COUNT; i++) setPixelScaled(i, layerColors[activeLayer], 255);
    else
      for(i=0; i<NEO_COUNT; i++) setPixel(i, 0, 0, 0);
  }

  // Bindings that don't fit in memory: red blinks.
  if(errorFlashFrames) {
    errorFlashFrames--;
    for(i=0; i<NEO_COUNT; i++)
      setPixel(i, errorFlashFrames % 10 >= 5 ? 255 : 0, 0, 0);
  }

  for(i=0; i<NEO_COUNT; i++) {
    if(overrideMask & (1 << i)) setPixelScaled(i, overrideColors[i], 255);
  }

  EA = 0;
  for(i=0; i<NEO_COUNT; i++) {
    NEO_writeColor(gamma8[((uint16_t)pixels[i][0] * light.brightness) >> 8],
                   gamma8[((uint16_t)pixels[i][1] * light.brightness) >> 8],
                   gamma8[((uint16_t)pixels[i][2] * light.brightness) >> 8]);
  }
  EA = 1;
}

void setLayer(uint8_t layer) {
  if(layer >= LAYERS) return;
  editLayer = layer;
  if(layer == activeLayer) return;
  releaseAll();
  activeLayer = layer;
  if(light.flags & FLAG_LAYERFLASH) layerFlashFrames = (layer + 1) * 15;
}

void enterBootloader(void) {
  uint8_t i;
  EA = 0;
  for(i=0; i<NEO_COUNT * 3; i++) NEO_sendByte(100);   // all white
  BOOT_now();
}

// ===================================================================================
// Commands from MiniKeys
// ===================================================================================

__xdata uint8_t cmd[CMD_LEN];

uint8_t controlIndex(uint8_t key) {
  if(key >= 1 && key <= 3) return key - 1;
  if(key >= 13 && key <= 15) return key - 10;     // 13, 14, 15 -> 3, 4, 5
  return 0xFF;
}

void handleCommand(void) {
  uint8_t ctrl, layer, type, i;
  __xdata uint8_t* s;

  switch(cmd[0]) {
    case 0xA1:                                    // A1 L: select (and activate) layer
      if(cmd[1] >= 1) setLayer(cmd[1] - 1);
      return;

    case 0xAA:                                    // AA AA / AA A1: save
      if(!saveSettings()) errorFlashFrames = 50;
      return;

    case 0xB0:                                    // B0 (L<<4|8) mode: stock light modes
      light.effect = cmd[2] == 0 ? FX_OFF : cmd[2] == 1 ? FX_PRESS : FX_WAVE;
      light.flags |= FLAG_MULTICOLOR;
      return;

    case 0xB1:                                    // B1 effect r g b speed brightness flags
      if(cmd[1] < FX_COUNT) light.effect = cmd[1];
      light.r = cmd[2]; light.g = cmd[3]; light.b = cmd[4];
      if(cmd[5] >= 1 && cmd[5] <= 10) light.speed = cmd[5];
      light.brightness = cmd[6];
      light.flags = cmd[7];
      effectStart = millis();
      return;

    case 0xB5:                                    // B5 on: press and turn switches layers
      light.reserved = COMBO_SET | (cmd[1] ? 1 : 0);
      return;

    case 0xB4:                                    // B4: restart effects now (sync with the app)
      effectStart = millis();
      return;

    case 0xB2:                                    // B2 key r g b: live color (key 0 = all)
      if(cmd[1] == 0xFF) { overrideMask = 0; return; }
      for(i=0; i<NEO_COUNT; i++) {
        if(cmd[1] == 0 || cmd[1] == i + 1) {
          ctrl = keyPixel[i];
          overrideColors[ctrl][0] = cmd[2];
          overrideColors[ctrl][1] = cmd[3];
          overrideColors[ctrl][2] = cmd[4];
          overrideMask |= 1 << ctrl;
        }
      }
      return;

    case 0xB3:                                    // B3 L r g b: layer color
      if(cmd[1] >= 1 && cmd[1] <= LAYERS) {
        layerColors[cmd[1] - 1][0] = cmd[2];
        layerColors[cmd[1] - 1][1] = cmd[3];
        layerColors[cmd[1] - 1][2] = cmd[4];
      }
      return;

    case 0xBB:                                    // BB 4D 4B: bootloader
      if(cmd[1] == 0x4D && cmd[2] == 0x4B) enterBootloader();
      return;
  }

  // K (L<<4|type) ...: key binding
  ctrl = controlIndex(cmd[0]);
  layer = (cmd[1] >> 4) - 1;
  type = cmd[1] & 0x0F;
  if(ctrl == 0xFF || layer >= LAYERS) return;
  setLayer(layer);
  s = bindings[layer * CONTROLS + ctrl];
  switch(type) {
    case TYPE_KEYS:                               // count index modifiers key
      if(cmd[2] > 5) return;
      if(cmd[3] == 0) {
        s[0] = TYPE_KEYS << 5 | (cmd[2] * 2);
        for(i=1; i<SLOT_LEN; i++) s[i] = 0;
      }
      else if(cmd[3] <= cmd[2]) {
        s[cmd[3] * 2 - 1] = cmd[4];
        s[cmd[3] * 2] = cmd[5];
      }
      break;
    case TYPE_MEDIA:                              // usage low, high
      s[0] = TYPE_MEDIA << 5 | 2;
      s[1] = cmd[2]; s[2] = cmd[3];
      break;
    case TYPE_MOUSE:                              // buttons x y wheel modifiers
      s[0] = TYPE_MOUSE << 5 | 5;
      for(i=0; i<5; i++) s[1 + i] = cmd[2 + i];
      break;
  }
}

// ===================================================================================
// Inputs
// ===================================================================================

#define DEBOUNCE_MS 5

__xdata uint8_t buttonStable[4];             // 1 = pressed: key 1, key 2, key 3, knob
__xdata uint8_t buttonCount[4];
__idata uint8_t encoderState;
__idata int8_t  encoderSteps = 0;

// Quadrature transitions: index = previous state << 2 | current state.
__code int8_t encoderTable[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};

// Layer switching on the keypad (when on): pressing and turning the knob switches to the next or
// previous saved layer instead of acting; a knob press without a turn acts when it's let go.
__idata uint8_t knobDown = 0;
__idata uint8_t comboUsed = 0;

// Next (+1) or previous (-1) saved layer; from the RAM layer, the first saved one.
void cycleLayer(int8_t direction) {
  uint8_t next;
  if(activeLayer >= SAVED_LAYERS) next = 0;
  else if(direction > 0) next = activeLayer + 1 >= SAVED_LAYERS ? 0 : activeLayer + 1;
  else next = activeLayer == 0 ? SAVED_LAYERS - 1 : activeLayer - 1;
  releaseAll();
  editLayer = next;
  activeLayer = next;
  layerFlashFrames = (next + 1) * 15;         // blink the layer number in its color
  EVT_sendLayer(next + 1);
}

uint8_t readEncoder(void) {
  return (PIN_read(PIN_ENC_A) ? 2 : 0) | (PIN_read(PIN_ENC_B) ? 1 : 0);
}

void pollEncoder(void) {
  uint8_t state = readEncoder();
  if(state == encoderState) return;
  encoderSteps += encoderTable[encoderState << 2 | state];
  encoderState = state;
  // Report a detent when the encoder rests again (both contacts open).
  if(state == 3) {
    if(knobDown && (encoderSteps <= -2 || encoderSteps >= 2)) {   // press and turn: switch layers
      cycleLayer(encoderSteps <= -2 ? 1 : -1);
      comboUsed = 1;
    }
    else if(encoderSteps <= -2) { controlDown(CTRL_KNOB_RIGHT); controlUp(CTRL_KNOB_RIGHT); flashPress(2); }
    else if(encoderSteps >= 2) { controlDown(CTRL_KNOB_LEFT); controlUp(CTRL_KNOB_LEFT); flashPress(2); }
    encoderSteps = 0;
  }
}

uint8_t readButton(uint8_t i) {
  switch(i) {
    case 0:  return !PIN_read(PIN_KEY1);
    case 1:  return !PIN_read(PIN_KEY2);
    case 2:  return !PIN_read(PIN_KEY3);
    default: return !PIN_read(PIN_ENC_SW);
  }
}

void pollButtons(void) {
  uint8_t i, pressed, switching = light.reserved & 0x01;
  for(i=0; i<4; i++) {
    pressed = readButton(i);
    if(pressed == buttonStable[i]) { buttonCount[i] = 0; continue; }
    if(++buttonCount[i] < DEBOUNCE_MS) continue;
    buttonStable[i] = pressed;
    buttonCount[i] = 0;

    if(i == 3 && switching) {                 // knob switch: act on release unless turned
      if(pressed) {
        knobDown = 1;
        comboUsed = 0;
      } else {
        knobDown = 0;
        if(!comboUsed) {
          flashPress(2);
          controlDown(CTRL_KNOB_PRESS);
          controlUp(CTRL_KNOB_PRESS);
        }
      }
      continue;
    }

    if(pressed) { flashPress(i < 3 ? i : 2); controlDown(i < 3 ? i : CTRL_KNOB_PRESS); }
    else controlUp(i < 3 ? i : CTRL_KNOB_PRESS);
  }
}

// ===================================================================================
// Pin finder (make DIAG=1): types the name of every free pin that gets pulled low
// ===================================================================================
#ifdef DIAG
__code uint8_t diagPins[] = {P10, P11, P12, P13, P14, P15, P16, P17, P30, P31, P32, P33, P35};
__code uint8_t diagNames[][2] = {{1,0},{1,1},{1,2},{1,3},{1,4},{1,5},{1,6},{1,7},
                                 {3,0},{3,1},{3,2},{3,3},{3,5}};
__xdata uint8_t diagLast[sizeof(diagPins)];

uint8_t diagRead(uint8_t pin) {
  switch(pin) {
    case P10: return PIN_read(P10); case P11: return PIN_read(P11);
    case P12: return PIN_read(P12); case P13: return PIN_read(P13);
    case P14: return PIN_read(P14); case P15: return PIN_read(P15);
    case P16: return PIN_read(P16); case P17: return PIN_read(P17);
    case P30: return PIN_read(P30); case P31: return PIN_read(P31);
    case P32: return PIN_read(P32); case P33: return PIN_read(P33);
    default:  return PIN_read(P35);
  }
}

void diagType(uint8_t usage) {
  sendKeys(0, usage);
  sendKeys(0, 0);
}

void diagLoop(void) {
  uint8_t i, now;
  P1_MOD_OC = 0xFF; P1_DIR_PU = 0xFF;             // all of port 1: input with pull-up
  P3_MOD_OC |= 0x2F; P3_DIR_PU |= 0x2F;           // P3.0-3.3, P3.5 (not USB, not LEDs)
  DLY_ms(20);
  for(i=0; i<sizeof(diagPins); i++) diagLast[i] = diagRead(diagPins[i]);
  while(1) {
    for(i=0; i<sizeof(diagPins); i++) {
      now = diagRead(diagPins[i]);
      if(now != diagLast[i]) {
        diagLast[i] = now;
        if(!now) {                                // pulled low: type "p" port pin space
          diagType(0x13);
          diagType(0x1D + diagNames[i][0]);       // '1' = 0x1E
          diagType(diagNames[i][1] ? 0x1D + diagNames[i][1] : 0x27);
          diagType(0x2C);
        }
      }
    }
    while(CMD_take(cmd)) handleCommand();         // keeps BB 4D 4B working
    DLY_ms(5);
  }
}
#endif

// ===================================================================================
// Main
// ===================================================================================

void main(void) {
  uint8_t i;
  uint32_t lastFrame = 0, now;

  NEO_init();
  CLK_config();
  DLY_ms(10);

  PIN_input_PU(PIN_KEY1);
  PIN_input_PU(PIN_KEY2);
  PIN_input_PU(PIN_KEY3);
  PIN_input_PU(PIN_ENC_A);
  PIN_input_PU(PIN_ENC_B);
  PIN_input_PU(PIN_ENC_SW);
  DLY_ms(5);

  if(!PIN_read(PIN_KEY1)) enterBootloader();

  for(i=0; i<CONTROLS; i++) { heldMods[i] = 0; heldKeys[i] = 0; heldButtons[i] = 0; }
  for(i=0; i<NEO_COUNT; i++) pressLevel[i] = 0;
  for(i=0; i<4; i++) { buttonStable[i] = 0; buttonCount[i] = 0; }
  encoderState = readEncoder();
  loadSettings();

  TIMER_init();
  HID_init();

  #ifdef DIAG
  DLY_ms(1500);                             // let the host finish enumerating
  diagLoop();
  #endif

  while(1) {
    for(i=0; i<4; i++) {                    // 1 ms, sampling the knob every 250 us
      pollEncoder();
      DLY_us(250);
    }
    pollButtons();
    while(CMD_take(cmd)) handleCommand();
    now = millis();
    if(now - lastFrame >= 20) {
      lastFrame = now;
      effectTime = now - effectStart;
      renderLights();
    }
  }
}
