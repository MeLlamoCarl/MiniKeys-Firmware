# ===================================================================================
# MiniKeys firmware for the CH552G 3-key + knob keypad
# Based on the makefile by Stefan Wagner (github.com/wagiminator), CC BY-SA 3.0.
# ===================================================================================
# make bin     build minikeys.bin
# make flash   build and upload (keypad in bootloader mode: hold key 1 while plugging in)
# make release copy the build to release/minikeys.bin for the flasher

SKETCH     = minikeys.c
TARGET     = minikeys
INCLUDE    = src
BUILD      = build

FREQ_SYS   = 16000000
XRAM_LOC   = 0x0100
XRAM_SIZE  = 0x0300
CODE_SIZE  = 0x3800

CC         = sdcc
OBJCOPY    = sdobjcopy
PACK_HEX   = packihx
WCHISP    ?= wchisp

CFLAGS  = -mmcs51 --model-small --no-xinit-opt --disable-warning 356 --disable-warning 357
CFLAGS += --xram-size $(XRAM_SIZE) --xram-loc $(XRAM_LOC) --code-size $(CODE_SIZE)
CFLAGS += -I$(INCLUDE) -DF_CPU=$(FREQ_SYS)
ifdef DIAG
CFLAGS += -DDIAG
TARGET  = minikeys-diag
BUILD   = build-diag
endif
CFILES  = $(SKETCH) $(wildcard $(INCLUDE)/*.c)
RFILES  = $(addprefix $(BUILD)/,$(notdir $(CFILES:.c=.rel)))

vpath %.c . $(INCLUDE)

bin: $(TARGET).bin size

$(BUILD)/%.rel: %.c $(wildcard $(INCLUDE)/*.h)
	@mkdir -p $(BUILD)
	@$(CC) -c $(CFLAGS) -o $@ $<

$(BUILD)/$(TARGET).ihx: $(RFILES)
	@$(CC) $(RFILES) $(CFLAGS) -o $@

$(TARGET).hex: $(BUILD)/$(TARGET).ihx
	@$(PACK_HEX) $< > $@

$(TARGET).bin: $(TARGET).hex
	@$(OBJCOPY) -I ihex -O binary $< $@

size: $(TARGET).bin
	@echo "FLASH: $$(stat -f %z $(TARGET).bin) of 14336 bytes"
	@awk '$$1 == "EXTERNAL" {print "XRAM:  " $(XRAM_LOC)+$$5 " bytes used"}' $(BUILD)/$(TARGET).mem

flash: $(TARGET).bin
	$(WCHISP) flash $(TARGET).bin

# Prebuilt image used by the flasher (committed, so installing doesn't need SDCC).
release: $(TARGET).bin
	@mkdir -p release
	@cp $(TARGET).bin release/minikeys.bin
	@echo "release/minikeys.bin updated"

clean:
	rm -rf $(BUILD) $(TARGET).hex $(TARGET).bin

.PHONY: bin size flash release clean
