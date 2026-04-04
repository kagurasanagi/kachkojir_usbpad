BUILD_DIR = build
TARGET = $(BUILD_DIR)/kachkojir_usbpad

# Toolchain definitions
TOOLCHAIN_PATH = C:/lib/xpack-riscv-none-elf-gcc-15.2.0-1/bin/riscv-none-elf-
CC = $(TOOLCHAIN_PATH)gcc
AS = $(TOOLCHAIN_PATH)gcc
OBJCOPY = $(TOOLCHAIN_PATH)objcopy
SIZE = $(TOOLCHAIN_PATH)size

# SDK Paths
SDK_DIR = SDK/EVT/EXAM/SRC
PERIPHERAL_DIR = $(SDK_DIR)/Peripheral
CORE_DIR = $(SDK_DIR)/Core
DEBUG_DIR = $(SDK_DIR)/Debug
STARTUP_DIR = $(SDK_DIR)/Startup
LD_DIR = $(SDK_DIR)/Ld

# Include paths
INCLUDES = \
	-Iinclude \
	-Isrc \
	-I$(CORE_DIR) \
	-I$(DEBUG_DIR) \
	-I$(PERIPHERAL_DIR)/inc

# Source files
SRCS = \
	$(wildcard src/*.c) \
	$(wildcard $(PERIPHERAL_DIR)/src/*.c) \
	$(DEBUG_DIR)/debug.c

ASMS = \
	$(STARTUP_DIR)/startup_ch32x035.S

# Object files (place in build directory)
OBJS = $(addprefix $(BUILD_DIR)/, $(SRCS:.c=.o)) $(addprefix $(BUILD_DIR)/, $(ASMS:.S=.o))

# Compilation flags
CFLAGS = -march=rv32ec_zicsr_zifencei -mabi=ilp32e -msmall-data-limit=8 -mno-save-restore -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -Wunused -Wuninitialized -g $(INCLUDES)
ASFLAGS = -march=rv32ec_zicsr_zifencei -mabi=ilp32e -x assembler-with-cpp $(INCLUDES) -c
LDFLAGS = -march=rv32ec_zicsr_zifencei -mabi=ilp32e -T $(LD_DIR)/Link.ld -nostartfiles -Xlinker --gc-sections -Wl,-Map,$(TARGET).map --specs=nano.specs --specs=nosys.specs

.PHONY: all clean flash

all: $(BUILD_DIR) $(TARGET).elf $(TARGET).bin $(TARGET).hex

$(BUILD_DIR):
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	@if not exist $(BUILD_DIR)\src mkdir $(BUILD_DIR)\src
	@if not exist $(BUILD_DIR)\SDK\EVT\EXAM\SRC\Peripheral\src mkdir $(BUILD_DIR)\SDK\EVT\EXAM\SRC\Peripheral\src
	@if not exist $(BUILD_DIR)\SDK\EVT\EXAM\SRC\Debug mkdir $(BUILD_DIR)\SDK\EVT\EXAM\SRC\Debug
	@if not exist $(BUILD_DIR)\SDK\EVT\EXAM\SRC\Startup mkdir $(BUILD_DIR)\SDK\EVT\EXAM\SRC\Startup

$(TARGET).elf: $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	$(SIZE) $@

$(BUILD_DIR)/%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.S
	$(AS) $(ASFLAGS) $< -o $@

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@

%.hex: %.elf
	$(OBJCOPY) -O ihex $< $@

clean:
	@if exist $(BUILD_DIR) rd /s /q $(BUILD_DIR)

flash: $(TARGET).bin
	# Example using minichlink or wch-openocd
	# minichlink -w $(TARGET).bin 0x08000000 -r
	@echo "Flashing: $(TARGET).bin"
