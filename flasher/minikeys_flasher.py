#!/usr/bin/env python3
"""MiniKeys Flasher: installs and updates the MiniKeys firmware on the CH552G 3-key + knob keypad.

    python3 flasher/minikeys_flasher.py status     what is connected
    python3 flasher/minikeys_flasher.py install    guided install (stock firmware) or update
    python3 flasher/minikeys_flasher.py backup     save the keypad's data flash (settings)
    python3 flasher/minikeys_flasher.py flash FILE write another firmware image

The WCH bootloader protocol follows the documentation of the open-source wchisp project.
"""

import argparse
import datetime
import os
import random
import subprocess
import sys
import time

try:
    import libusb_package
    import usb.core
    import usb.util
except ImportError:
    sys.exit("Missing dependencies. Run: python3 -m pip install -r flasher/requirements.txt")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_FIRMWARE = os.path.join(ROOT, "Firmware", "release", "minikeys.bin")
BACKUP_DIR = os.path.join(ROOT, "backups")
IMAGES = os.path.join(ROOT, "docs", "images")

KEYPAD_VID, KEYPAD_PID = 0x1189, 0x8890
BOOTLOADER_VIDS, BOOTLOADER_PID = (0x4348, 0x1A86), 0x55E0
MINIKEYS_BCD = 0x20                     # bcdDevice 0x20xx = MiniKeys firmware, xx = version

BACKEND = libusb_package.get_libusb1_backend()


# ---------------------------------------------------------------------------------------------
# Devices
# ---------------------------------------------------------------------------------------------

def find_keypad():
    return usb.core.find(idVendor=KEYPAD_VID, idProduct=KEYPAD_PID, backend=BACKEND)


def find_bootloader():
    for vid in BOOTLOADER_VIDS:
        device = usb.core.find(idVendor=vid, idProduct=BOOTLOADER_PID, backend=BACKEND)
        if device is not None:
            return device
    return None


def firmware_version(keypad):
    """MiniKeys firmware version, or None for the stock firmware."""
    bcd = keypad.bcdDevice
    return bcd & 0xFF if bcd >> 8 == MINIKEYS_BCD else None


def describe():
    if find_bootloader() is not None:
        return "bootloader", "Keypad in bootloader mode"
    keypad = find_keypad()
    if keypad is None:
        return "none", "No keypad connected"
    version = firmware_version(keypad)
    if version is None:
        return "stock", "Keypad with the stock firmware"
    return "minikeys", "Keypad with MiniKeys firmware v%d" % version


def reboot_into_bootloader(keypad):
    """Sends BB 4D 4B to the MiniKeys firmware's programming interface (interface 1)."""
    try:
        usb.util.claim_interface(keypad, 1)
    except usb.core.USBError as error:
        raise SystemExit("Couldn't open the keypad (%s). Quit MiniKeys and try again." % error)
    packet = bytes([0x03, 0xBB, 0x4D, 0x4B]) + bytes(61)
    try:
        keypad.write(0x02, packet, timeout=1000)
    except usb.core.USBError:
        pass                                # the keypad restarts before the transfer ends
    usb.util.dispose_resources(keypad)


def wait_for(predicate, timeout, label=None):
    deadline = time.time() + timeout
    while time.time() < deadline:
        result = predicate()
        if result:
            return result
        if label:
            remaining = int(deadline - time.time())
            print("\r  %s (%d:%02d left) " % (label, remaining // 60, remaining % 60), end="", flush=True)
        time.sleep(0.3)
    if label:
        print()
    return None


# ---------------------------------------------------------------------------------------------
# WCH ISP bootloader
# ---------------------------------------------------------------------------------------------

class Bootloader:
    CH552, CH55X = 0x52, 0x11
    SECTOR, CHUNK = 1024, 56
    DATA_FLASH_SIZE = 128

    IDENTIFY, END, KEY, ERASE, PROGRAM, VERIFY, READ_CONFIG, DATA_READ = (
        0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xAB)

    def __init__(self, device):
        self.device = device
        try:
            device.set_configuration()
        except usb.core.USBError:
            pass                            # already configured
        usb.util.claim_interface(device, 0)
        self.key = bytes(8)

    def close(self):
        usb.util.dispose_resources(self.device)

    def transfer(self, command, payload=b"", timeout=2000):
        request = bytes([command]) + len(payload).to_bytes(2, "little") + bytes(payload)
        self.device.write(0x02, request, timeout=timeout)
        reply = bytes(self.device.read(0x82, 64, timeout=timeout))
        if len(reply) < 4 or reply[0] != command or int.from_bytes(reply[2:4], "little") != len(reply) - 4:
            raise RuntimeError("Unexpected reply to command 0x%02X" % command)
        return reply[4:]

    def connect(self):
        """Identifies the chip and derives the XOR key from its unique ID."""
        identity = self.transfer(self.IDENTIFY, bytes([self.CH552, self.CH55X]) + b"MCU ISP & WCH.CN")
        if identity[:2] != bytes([self.CH552, self.CH55X]):
            raise RuntimeError("This isn't a CH552 (chip 0x%02X, type 0x%02X)" % (identity[0], identity[1]))
        # Reply: 2 bytes, 12 config bytes, 4 bootloader version bytes, then the unique ID.
        config = self.transfer(self.READ_CONFIG, bytes([0x1F, 0x00]))
        uid_sum = sum(config[18:22]) & 0xFF                  # CH55x: 4-byte UID
        key = [uid_sum] * 8
        key[7] = (key[7] + self.CH552) & 0xFF
        self.key = bytes(key)
        version = config[14:18]
        return "CH552, bootloader %d.%d%d" % (version[1], version[2] >> 4, version[2] & 0xF)

    def read_data_flash(self):
        data = b""
        while len(data) < self.DATA_FLASH_SIZE:
            length = min(0x3A, self.DATA_FLASH_SIZE - len(data))
            reply = self.transfer(self.DATA_READ, len(data).to_bytes(4, "little") + length.to_bytes(2, "little"))
            data += reply[2:]
        return data

    def _send_key(self):
        reply = self.transfer(self.KEY, bytes(0x1E))
        if reply[0] != sum(self.key) & 0xFF:
            raise RuntimeError("The bootloader rejected the session key")

    def _xor(self, chunk):
        return bytes(b ^ self.key[i % 8] for i, b in enumerate(chunk))

    def _chunk_command(self, command, address, chunk, timeout=2000):
        payload = address.to_bytes(4, "little") + bytes([random.randrange(256)]) + self._xor(chunk)
        return self.transfer(command, payload, timeout=timeout)

    def write_firmware(self, firmware, progress):
        image = firmware + b"\xff" * (-len(firmware) % self.SECTOR)
        progress(0.0, "Erasing")
        self.transfer(self.ERASE, max(8, len(image) // self.SECTOR + 1).to_bytes(4, "little"), timeout=5000)
        time.sleep(1)

        self._send_key()
        for address in range(0, len(image), self.CHUNK):
            self._chunk_command(self.PROGRAM, address, image[address:address + self.CHUNK], timeout=500)
            progress(0.1 + 0.6 * (address + self.CHUNK) / len(image), "Writing")
        self._chunk_command(self.PROGRAM, len(image), b"", timeout=500)
        time.sleep(0.5)

        self._send_key()
        for address in range(0, len(image), self.CHUNK):
            reply = self._chunk_command(self.VERIFY, address, image[address:address + self.CHUNK])
            if reply[0] != 0:
                raise RuntimeError("Verification failed at 0x%04X" % address)
            progress(0.7 + 0.3 * (address + self.CHUNK) / len(image), "Verifying")

    def restart(self):
        try:
            self.transfer(self.END, bytes([0x01]), timeout=500)
        except (usb.core.USBError, RuntimeError):
            pass                            # the chip leaves the bootloader right away


# ---------------------------------------------------------------------------------------------
# Commands
# ---------------------------------------------------------------------------------------------

def show_image(name, open_images):
    path = os.path.join(IMAGES, name)
    print("     Photo: docs/images/%s" % name)
    if open_images and sys.platform == "darwin":
        subprocess.run(["open", path], check=False)


def progress_bar(fraction, phase):
    filled = int(min(fraction, 1.0) * 30)
    print("\r  %-10s [%s%s] %3d%%" % (phase, "#" * filled, "." * (30 - filled), min(fraction, 1.0) * 100),
          end="", flush=True)


def save_backup(data, label):
    os.makedirs(BACKUP_DIR, exist_ok=True)
    name = "dataflash-%s-%s.bin" % (label, datetime.datetime.now().strftime("%Y%m%d-%H%M%S"))
    with open(os.path.join(BACKUP_DIR, name), "wb") as file:
        file.write(data)
    return os.path.join("backups", name)


def guide_to_bootloader(open_images):
    print("""
The stock firmware can only be replaced by putting the chip in bootloader mode by hand. This is a
one-time step: afterwards MiniKeys and this tool update the keypad over USB.

  Heads up: the stock firmware can't be backed up or restored. Its settings (data flash) are saved.

  1. Unplug the keypad and remove the bottom plate (4 screws). Find the CH552G chip (U7).""")
    show_image("board-chip.jpg", open_images)
    print("""
  2. With a 10 kΩ resistor (or fine tweezers), connect pin 12 (D+) to pin 11 (3.3 V).
     Don't touch pin 10 (5 V).""")
    show_image("bootloader-pins.jpg", open_images)
    print("""
  3. Keep the contact and plug the keypad in. Let go after a second: the LEDs stay off in
     bootloader mode. If they light up as usual, unplug and try again.

     Older chips use pin 3 (P1.5) to pin 9 (GND) instead.""")
    show_image("bootloader-pins-alt.jpg", open_images)
    print()


def flash(firmware_path, from_stock):
    with open(firmware_path, "rb") as file:
        firmware = file.read()
    device = find_bootloader()
    bootloader = Bootloader(device)
    try:
        print("  Connected: %s" % bootloader.connect())
        data = bootloader.read_data_flash()
        print("  Settings backed up to %s" % save_backup(data, "stock" if from_stock else "previous"))
        bootloader.write_firmware(firmware, progress_bar)
        print("\n  Firmware written and verified (%d bytes)" % len(firmware))
        bootloader.restart()
    finally:
        bootloader.close()


def command_status(_args):
    _, text = describe()
    print(text)


def command_backup(_args):
    if find_bootloader() is None:
        keypad = find_keypad()
        if keypad is None or firmware_version(keypad) is None:
            sys.exit("Put the keypad in bootloader mode first (see `install`).")
        reboot_into_bootloader(keypad)
        if wait_for(find_bootloader, 10) is None:
            sys.exit("The keypad didn't enter bootloader mode.")
    bootloader = Bootloader(find_bootloader())
    try:
        bootloader.connect()
        print("Saved %s" % save_backup(bootloader.read_data_flash(), "backup"))
        bootloader.restart()
    finally:
        bootloader.close()


def command_install(args):
    firmware = args.firmware or DEFAULT_FIRMWARE
    if not os.path.exists(firmware):
        sys.exit("Firmware not found: %s (build it with `make -C Firmware release`)" % firmware)
    state, text = describe()
    print(text)
    from_stock = state in ("stock", "none")

    if state == "minikeys":
        print("Restarting the keypad in bootloader mode…")
        reboot_into_bootloader(find_keypad())
        if wait_for(find_bootloader, 10) is None:
            sys.exit("The keypad didn't enter bootloader mode. Hold key 1 while plugging it in, then run this again.")
    elif state != "bootloader":
        guide_to_bootloader(not args.no_images)
        if wait_for(find_bootloader, 300, "Waiting for the keypad in bootloader mode") is None:
            sys.exit("No keypad in bootloader mode. Nothing was changed.")
        print("\n  Keypad found in bootloader mode")

    flash(firmware, from_stock)

    keypad = wait_for(lambda: find_keypad(), 10)
    if keypad is not None and firmware_version(keypad) is not None:
        print("Done: MiniKeys firmware v%d is running. Open MiniKeys to set it up." % firmware_version(keypad))
    else:
        print("Done. Unplug the keypad and plug it in again if it doesn't show up.")


def command_flash(args):
    if find_bootloader() is None:
        keypad = find_keypad()
        if keypad is not None and firmware_version(keypad) is not None:
            reboot_into_bootloader(keypad)
        if wait_for(find_bootloader, 10) is None:
            sys.exit("Put the keypad in bootloader mode first (see `install`).")
    flash(args.file, from_stock=False)


def main():
    parser = argparse.ArgumentParser(description="Install or update the MiniKeys keypad firmware.")
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("status", help="show what is connected").set_defaults(run=command_status)
    install = sub.add_parser("install", help="install or update the MiniKeys firmware")
    install.add_argument("--firmware", help="firmware image (default: Firmware/release/minikeys.bin)")
    install.add_argument("--no-images", action="store_true", help="don't open the reference photos")
    install.set_defaults(run=command_install)
    sub.add_parser("backup", help="save the keypad's data flash").set_defaults(run=command_backup)
    flash_parser = sub.add_parser("flash", help="write a firmware image")
    flash_parser.add_argument("file")
    flash_parser.set_defaults(run=command_flash)
    args = parser.parse_args()
    try:
        args.run(args)
    except KeyboardInterrupt:
        print("\nCancelled.")
    except (usb.core.USBError, RuntimeError) as error:
        sys.exit("\nError: %s" % error)


if __name__ == "__main__":
    main()
