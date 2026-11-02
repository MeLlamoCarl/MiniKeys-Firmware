# MiniKeys firmware

Replacement firmware for the CH552G 3-key + knob keypad (USB `1189:8890`). It speaks the same
programming protocol as the stock firmware (see MiniKeys'
[PROTOCOL.md](https://github.com/MeLlamoCarl/MiniKeys/blob/main/PROTOCOL.md)), so MiniKeys keeps
working, and adds full control of the three WS2812 RGB LEDs. To install it, see the
[main README](../README.md).

## What it adds

| Command | Bytes | Effect |
|---|---|---|
| Light effect | `B1 effect r g b speed brightness flags` | effect: 0 off, 1 on press, 2 wave, 3 solid, 4 breathe, 5 rainbow, 6 layer color; speed 1–10; flags: `01` multicolor, `02` blink the layer number on layer changes. Save with `AA A1`. |
| Live key color | `B2 key r g b` (`key` 1–3, 0 = all; `B2 FF` clears) | Overrides the effect until cleared; not saved. |
| Layer color | `B3 L r g b` | Used by the layer effect and the layer blink. Save with `AA A1`. |
| Sync | `B4` | Restarts the light effect now (firmware 3+). `B1` does too. |
| Layer switching | `B5 on` (`1` on, `0` off) | Press and turn the knob to switch between the 3 saved layers (firmware 5+). While it's on, a knob press acts when the knob is released, if it wasn't turned. Save with `AA A1`. |
| Bootloader | `BB 4D 4B` | Reboots into the WCH bootloader for updates. |

`A1 L` also makes layer `L` active right away (the stock firmware only switches when a key of
that layer is written). Layers 1–3 are saved; layer 4 lives in RAM only, so MiniKeys can run
layers beyond the first 3 without writing the flash.

When the layer changes on the keypad, the firmware sends `4C L` on interface 1's interrupt IN
endpoint (EP3), so the app can follow (firmware 5+). Bindings, light settings and layer colors live in the 128-byte data flash;
bindings take 1 byte plus their payload each, so very long sequences on every key may not fit
(the keypad blinks red and keeps the last saved state).

The firmware reports `bcdDevice` `0x20xx` and the product name "MiniKeys Keypad", which is how
MiniKeys tells it apart from the stock firmware.

## Build

Requires SDCC (`brew install sdcc`). `make bin` builds `minikeys.bin`; `make release` copies it to
`release/minikeys.bin`, which the flasher installs. `make flash` uploads with
[wchisp](https://github.com/ch32-rs/wchisp) instead, if you have it.

## Timing

Effects run on a 1 ms hardware timer (timer 2) and are computed from the time since the last `B1`
or `B4`, with integer math that MiniKeys' preview repeats: breathing 6–1.2 s per breath, rainbow
20–2 s per turn, wave 400–100 ms per key (speed 1–10), gamma 2.6 on the output. MiniKeys sends `B4`
shortly after any light change and restarts its preview at that moment, so the keypad and the
screen show the same frame.

## Pins

| Function | Pin |
|---|---|
| Keys 1, 2, 3 | P1.1, P1.7, P1.6 |
| Knob A, B, switch | P3.0, P3.1, P3.3 |
| WS2812 data (3 pixels) | P3.4 |

## Credits and license

Pin mapping from biemster's [RGB Macropad Custom Firmware](https://hackaday.io/project/189914).
The CH55x libraries in `src/` come from Stefan Wagner's [CH552 projects](https://github.com/wagiminator).
The firmware is licensed under [CC BY-SA 3.0](LICENSE); `ch554.h` is WCH's register header.
