# MARF (Buchla 248r) firmware — command-line build
#
# Usage:
#   make            # build build/MARF.elf + .hex + .bin and print size (v2)
#   make rev1       # build the REV1 (v1 hardware) image into build-rev1/
#   make clean      # remove the build directory
#   make size       # re-print the size of the built elf
#
# Hardware revision is selected with MARF_HW (default 2). MARF_HW=1 targets the
# original v1.x board (REV1); see src/marf_version.h. Validated on real v1
# hardware as of v3.2.1.
#
# Toolchain: the Arm GNU bare-metal toolchain (arm-none-eabi-*) with newlib.
# Override the prefix or its location if it is not on PATH, e.g.
#   make TOOLCHAIN_PATH=/opt/arm/bin/
#   make CROSS=arm-none-eabi-

# ---- Toolchain --------------------------------------------------------------
TOOLCHAIN_PATH ?=
CROSS          ?= arm-none-eabi-
CC      = $(TOOLCHAIN_PATH)$(CROSS)gcc
OBJCOPY = $(TOOLCHAIN_PATH)$(CROSS)objcopy
SIZE    = $(TOOLCHAIN_PATH)$(CROSS)size

# ---- Project ----------------------------------------------------------------
TARGET     ?= MARF
BUILD_DIR  ?= build
LDSCRIPT   = stm32f4_flash.ld

# Source trees
SRC_DIR    = src
DRV_DIR    = Libraries/STM32F4xx_StdPeriph_Driver/src

C_SOURCES  = $(wildcard $(SRC_DIR)/*.c) $(wildcard $(DRV_DIR)/*.c)
ASM_SOURCES = $(SRC_DIR)/startup_stm32f40xx.s

INCLUDES = \
  -I$(SRC_DIR) \
  -ILibraries/STM32F4xx_StdPeriph_Driver/inc \
  -ILibraries/Device/STM32F4xx/Include \
  -ILibraries/CMSIS/Include

# Target hardware revision: 2 (default, SAModular/EMS v2) or 1 (v1.x board).
MARF_HW ?= 2

# 200e preset-bus attachment (docs/DESIGN-200e-bus.md). Off by default; build
# with BUS200E_ENABLE=1 to compile the bus code in. The first enabled build is
# RX-log-only (decoded bus commands land in a debug ring; no actions taken).
BUS200E_ENABLE ?= 0
# BUS200E_DIAG=1 (with BUS200E_ENABLE=1) paints the bus pin levels and counters
# on the step LEDs for bring-up without SWD; see src/i2c_bb.h. Never ship it.
BUS200E_DIAG ?= 0
# BUS200E_RXLOG_ONLY=1 keeps the bus receive-only: commands are decoded into the
# debug ring but never act on a preset. Implied by BUS200E_DIAG. Use it to prove
# the wiring before letting the bus write to the EEPROM.
BUS200E_RXLOG_ONLY ?= 0
# Which pins the bus is wired to. Names read SCL then SDA.
#   auto      per-hardware default: pa10pb3 on v2, pb34 on v1
#   pa10pb3   TO COMPUTER header: SCL = pin 2 (PA10), SDA = pin 4 (PB3)
#   pb3pa10   the same two pins with clock and data swapped
#   pb34      STLINK header: SCL = pin 13 (PB3), SDA = pin 3 (PB4)
#   pa910     contradicted by the v2.5 schematic (PA9 has no net); do not use
#             unless a board revision is known to route it
# See docs/200e-BUS-WIRING.md.
# Bench forensics for the bus transport (never ship): records which code path
# left an ACK asserted when the stretch failsafe fires. See src/i2c_bb.c.
BUS200E_FORENSIC ?= 0

BUS200E_PINS ?= auto
ifeq ($(BUS200E_PINS),pb34)
  BUS200E_PIN_DEF = -DBUS200E_PINS_PB3_PB4=1
else ifeq ($(BUS200E_PINS),pa10pb3)
  BUS200E_PIN_DEF = -DBUS200E_PINS_PA10_PB3=1
else ifeq ($(BUS200E_PINS),pb3pa10)
  BUS200E_PIN_DEF = -DBUS200E_PINS_PB3_PA10=1
else ifeq ($(BUS200E_PINS),pa910)
  BUS200E_PIN_DEF = -DBUS200E_PINS_PA9_PA10=1
else ifeq ($(BUS200E_PINS),auto)
  BUS200E_PIN_DEF =
else
  $(error BUS200E_PINS must be auto, pa10pb3, pb3pa10, pb34 or pa910 (got "$(BUS200E_PINS)"))
endif

DEFINES = -DSTM32F40XX -DSTM32F4XX -DUSE_STDPERIPH_DRIVER -DMARF_HW=$(MARF_HW) \
  -DBUS200E_ENABLE=$(BUS200E_ENABLE) -DBUS200E_DIAG=$(BUS200E_DIAG) \
  -DBUS200E_RXLOG_ONLY=$(BUS200E_RXLOG_ONLY) -DBUS200E_FORENSIC=$(BUS200E_FORENSIC) \
  $(BUS200E_PIN_DEF) $(DEFINES_EXTRA)

# ---- Flags ------------------------------------------------------------------
CPU = -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16

OPT ?= -O3

CFLAGS = $(CPU) $(OPT) -Wall \
  -ffunction-sections -fdata-sections \
  $(DEFINES) $(INCLUDES) -MMD -MP

ASFLAGS = $(CPU)

LDFLAGS = $(CPU) --specs=nano.specs -T$(LDSCRIPT) \
  -Wl,--gc-sections -Wl,-Map=$(BUILD_DIR)/$(TARGET).map -static

# ---- Objects ----------------------------------------------------------------
OBJECTS  = $(addprefix $(BUILD_DIR)/, $(notdir $(C_SOURCES:.c=.o)))
OBJECTS += $(addprefix $(BUILD_DIR)/, $(notdir $(ASM_SOURCES:.s=.o)))
# Let make find sources that live in two directories
vpath %.c $(SRC_DIR) $(DRV_DIR)
vpath %.s $(SRC_DIR)

# ---- Rules ------------------------------------------------------------------
.PHONY: all clean size test rev1 v16
all: $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin size

# ---- REV1 (v1 hardware) build -----------------------------------------------
# Builds the same source for the original v1.x board (MARF_HW=1) into a
# separate directory so the default v2 build is untouched. Validated on real
# v1 hardware as of v3.2.1.
rev1:
	$(MAKE) MARF_HW=1 BUILD_DIR=build-rev1 TARGET=MARF-REV1

# Deprecated alias
v16: rev1

# ---- Host unit tests --------------------------------------------------------
# Compiles the pure logic with the host compiler against test/shim (no target
# toolchain needed). Run with: make test
HOST_CC    ?= cc
TEST_SRC    = test/test_core.c test/test_storage.c test/test_scales.c test/test_turing.c \
              test/test_presets.c test/test_clockfollow.c test/test_afg_bench.c \
              test/test_v1_invariants.c test/test_bus200e.c test/test_support.c \
              src/program.c src/analog_data.c src/storage.c src/scales.c src/turing.c \
              src/presets.c src/clockfollow.c src/afg.c src/bus200e.c
# The host suite always builds the (pure) bus engine, whatever the target build
# gates it to.
TEST_CFLAGS = -std=c11 -Wall -Itest/shim -I$(SRC_DIR) -DBUS200E_ENABLE=1
# Link libraries must come AFTER the sources (GNU ld is order-sensitive).
TEST_LIBS   = -lm

# The suite builds and runs TWICE: once per hardware variant. Validating a
# change on a v2 bench plus a green `make test` covers the REV1 build's
# shared logic and its variant-specific contracts (see
# docs/V1-COMPATIBILITY.md).
test: | $(BUILD_DIR)
	$(HOST_CC) $(TEST_CFLAGS) $(TEST_SRC) $(TEST_LIBS) -o $(BUILD_DIR)/run_tests
	./$(BUILD_DIR)/run_tests
	$(HOST_CC) $(TEST_CFLAGS) -DMARF_HW=1 $(TEST_SRC) $(TEST_LIBS) -o $(BUILD_DIR)/run_tests_rev1
	./$(BUILD_DIR)/run_tests_rev1

# ---- User manual (PDF) ------------------------------------------------------
# Renders the docs/ markdown into build/MARF-Manual.pdf with pandoc + a LaTeX
# engine (tectonic by default). Inter-file .md links are flattened to plain text
# so they read cleanly in the single combined document.
.PHONY: manual
MANUAL_SRCS = \
  docs/01-overview.md \
  docs/02-installation-and-flashing.md \
  docs/03-calibration.md \
  docs/04-front-panel-reference.md \
  docs/05-programming-steps.md \
  docs/06-scales.md \
  docs/07-shift-register.md \
  docs/08-running-and-clocking.md \
  docs/09-saving-and-loading.md \
  docs/10-section-shift.md \
  docs/11-pulse-tricks.md \
  docs/12-troubleshooting.md
MANUAL_PDF      ?= $(BUILD_DIR)/MARF-Manual.pdf
MANUAL_DATE     ?= $(shell date +%Y-%m-%d)
MANUAL_VERSION  ?= $(shell git describe --tags --always 2>/dev/null || echo dev)
PANDOC_PDF_ENGINE ?= tectonic

manual: | $(BUILD_DIR)
	awk 'FNR==1 && NR!=1 {printf "\n\\newpage\n\n"} {print}' $(MANUAL_SRCS) \
	  | sed -E 's@\[([^]]+)\]\((\./)?[0-9][^)]*\.md[^)]*\)@\1@g' \
	  | pandoc --from markdown \
	      --pdf-engine=$(PANDOC_PDF_ENGINE) \
	      --toc --toc-depth=2 \
	      --metadata title="MARF — Multiple Arbitrary Function Generator" \
	      --metadata subtitle="User Manual ($(MANUAL_VERSION))" \
	      --metadata date="$(MANUAL_DATE)" \
	      -V geometry:margin=1in -V colorlinks=true -V linkcolor=blue \
	      -o $(MANUAL_PDF) -
	@echo "Wrote $(MANUAL_PDF)"

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.s | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

$(BUILD_DIR)/$(TARGET).hex: $(BUILD_DIR)/$(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD_DIR)/$(TARGET).bin: $(BUILD_DIR)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

size: $(BUILD_DIR)/$(TARGET).elf
	$(SIZE) $<

$(BUILD_DIR):
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)

-include $(OBJECTS:.o=.d)
