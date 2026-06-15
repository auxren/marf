# MARF (Buchla 248r) firmware — command-line build
#
# Usage:
#   make            # build build/MARF.elf + .hex + .bin and print size (v2)
#   make v16        # build the v1.6 (no-strobe) candidate into build-v1.6/
#   make clean      # remove the build directory
#   make size       # re-print the size of the built elf
#
# Hardware revision is selected with MARF_HW (default 2). MARF_HW=1 targets the
# original v1.x board; see src/marf_version.h. The v1 build is reverse-
# engineered and UNVERIFIED on real v1 hardware.
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
DEFINES = -DSTM32F40XX -DSTM32F4XX -DUSE_STDPERIPH_DRIVER -DMARF_HW=$(MARF_HW)

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
.PHONY: all clean size test v16
all: $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin size

# ---- v1.6 (no-strobe) candidate build --------------------------------------
# Builds the same source for the original v1.x board (MARF_HW=1) into a
# separate directory so the default v2 build is untouched. The v1 pin map is
# reverse-engineered and UNVERIFIED on real v1 hardware.
v16:
	$(MAKE) MARF_HW=1 BUILD_DIR=build-v1.6 TARGET=MARF-REV1-no-strobe

# ---- Host unit tests --------------------------------------------------------
# Compiles the pure logic with the host compiler against test/shim (no target
# toolchain needed). Run with: make test
HOST_CC    ?= cc
TEST_SRC    = test/test_core.c test/test_storage.c test/test_scales.c test/test_turing.c \
              test/test_presets.c test/test_support.c \
              src/program.c src/analog_data.c src/storage.c src/scales.c src/turing.c \
              src/presets.c
TEST_CFLAGS = -std=c11 -Wall -Itest/shim -I$(SRC_DIR)
# Link libraries must come AFTER the sources (GNU ld is order-sensitive).
TEST_LIBS   = -lm

test: | $(BUILD_DIR)
	$(HOST_CC) $(TEST_CFLAGS) $(TEST_SRC) $(TEST_LIBS) -o $(BUILD_DIR)/run_tests
	./$(BUILD_DIR)/run_tests

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
