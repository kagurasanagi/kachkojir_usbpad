BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj
TARGET = $(BUILD_DIR)/kachkojir_usbpad

# Toolchain definitions
TOOLCHAIN_PATH = C:/lib/xpack-riscv-none-elf-gcc-15.2.0-1/bin/riscv-none-elf-
CC = $(TOOLCHAIN_PATH)gcc
AS = $(TOOLCHAIN_PATH)gcc
OBJCOPY = $(TOOLCHAIN_PATH)objcopy
SIZE = $(TOOLCHAIN_PATH)size

# SDK Paths
SDK_DIR = SDK/EVT/EXAM/SRC
CORE_DIR = $(SDK_DIR)/Core
PERIPHERAL_DIR = $(SDK_DIR)/Peripheral
LD_DIR = $(SDK_DIR)/Ld

# Include paths
INCLUDES = \
	-Iinclude \
	-Iinclude/usb_host \
	-I$(CORE_DIR) \
	-I$(PERIPHERAL_DIR)/inc

# Source files (App)
SRCS = \
	src/main.c \
	src/debug.c \
	src/system_ch32x035.c \
	src/spi_slave.c \
	src/usbc_source.c

# Source files (USB Host Service Layer)
SRCS += \
	src/usb_host/ch32x035_usbfs_host.c \
	src/usb_host/ch32x035_it.c \
	src/usb_host/usb_host_gamepad.c \
	src/usb_host/gamepad_mapper.c \
	src/usb_host/usb_host_hid.c \
	src/usb_host/usb_host_hub.c

# Auto-compile all drop-in gamepad profiles
SRCS += $(wildcard src/profiles/*.c)

# Source files (SDK Peripherals)
SRCS += \
	$(PERIPHERAL_DIR)/src/ch32x035_gpio.c \
	$(PERIPHERAL_DIR)/src/ch32x035_exti.c \
	$(PERIPHERAL_DIR)/src/ch32x035_usart.c \
	$(PERIPHERAL_DIR)/src/ch32x035_rcc.c \
	$(PERIPHERAL_DIR)/src/ch32x035_spi.c \
	$(PERIPHERAL_DIR)/src/ch32x035_tim.c \
	$(PERIPHERAL_DIR)/src/ch32x035_pwr.c \
	$(PERIPHERAL_DIR)/src/ch32x035_dma.c \
	$(PERIPHERAL_DIR)/src/ch32x035_flash.c \
	$(PERIPHERAL_DIR)/src/ch32x035_misc.c

# Startup file
ASMS = $(SDK_DIR)/Startup/startup_ch32x035.S

# Object files (placed in OBJ_DIR)
OBJS = $(addprefix $(OBJ_DIR)/, $(notdir $(SRCS:.c=.o))) $(addprefix $(OBJ_DIR)/, $(notdir $(ASMS:.S=.o)))

# Search paths for source files (VPATH)
VPATH = src:src/usb_host:$(PERIPHERAL_DIR)/src:$(SDK_DIR)/Startup

# Compilation flags
CFLAGS = -march=rv32ec_zicsr_zifencei -mabi=ilp32e -msmall-data-limit=8 -mno-save-restore -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -Wunused -Wuninitialized -g $(INCLUDES) -DCH32X035
ASFLAGS = -march=rv32ec_zicsr_zifencei -mabi=ilp32e -x assembler-with-cpp $(INCLUDES) -c -DCH32X035
LDFLAGS = -march=rv32ec_zicsr_zifencei -mabi=ilp32e -T $(LD_DIR)/Link.ld -nostartfiles -Xlinker --gc-sections -Wl,-Map,$(TARGET).map --specs=nano.specs --specs=nosys.specs

.PHONY: all clean flash

all: $(BUILD_DIR) $(OBJ_DIR) $(TARGET).elf $(TARGET).bin $(TARGET).hex

$(BUILD_DIR):
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)

$(OBJ_DIR):
	@if not exist $(OBJ_DIR) mkdir build\obj

$(TARGET).elf: $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	$(SIZE) $@

# Build rules
$(OBJ_DIR)/%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: %.S
	$(AS) $(ASFLAGS) $< -o $@

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@

%.hex: %.elf
	$(OBJCOPY) -O ihex $< $@

clean:
	@if exist $(BUILD_DIR) rd /s /q $(BUILD_DIR)

flash: $(TARGET).bin
	@echo "Flashing: $(TARGET).bin"
