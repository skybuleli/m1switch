TARGET	:=	test_svc_sleep
SOURCES	:=	.
BUILD	:=	build
PREFIX	:=	aarch64-none-elf-
CC	:=	$(DEVKITPRO)/devkitA64/bin/$(PREFIX)gcc
LD	:=	$(DEVKITPRO)/devkitA64/bin/$(PREFIX)gcc
ARCH	:=	-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
LDFLAGS	:=	-specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(TARGET).map -nostartfiles -nostdlib

all: build/$(TARGET).nro

build/$(TARGET).nro: build/$(TARGET).elf
	$(DEVKITPRO)/tools/bin/elf2nro $< $@

build/$(TARGET).elf: main.S
	@mkdir -p build
	$(CC) $(ARCH) $(LDFLAGS) main.S -o $@

clean:
	rm -rf build $(TARGET).nro $(TARGET).elf
