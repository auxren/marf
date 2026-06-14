# MARF (Buchla 248r) firmware — command-line build
#
# Usage:
#   make            # build build/MARF.elf + .hex + .bin and print size
#   make clean      # remove the build directory
#   make size       # re-print the size of the built elf
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
.PHONY: all clean size v16
all: $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin size

# ---- v1.6 (no-strobe) candidate build --------------------------------------
v16:
	$(MAKE) MARF_HW=1 BUILD_DIR=build-v1.6 TARGET=MARF-v1.6-no-strobe

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
