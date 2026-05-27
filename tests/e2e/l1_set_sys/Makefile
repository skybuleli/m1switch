TARGET	:=	test_set_sys
PREFIX	:=	aarch64-none-elf-
CC	:=	/opt/devkitpro/devkitA64/bin/$(PREFIX)gcc
ARCH	:=	-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS	:=	-g -O2 -ffunction-sections $(ARCH) -I/opt/devkitpro/libnx/include -D__SWITCH__
LDFLAGS	:=	-specs=/opt/devkitpro/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(TARGET).map

all: build/$(TARGET).nro

build/$(TARGET).nro: build/$(TARGET).elf
	@mkdir -p build
	/opt/devkitpro/tools/bin/elf2nro $< $@

build/$(TARGET).elf: build/main.o
	@mkdir -p build
	$(CC) $(CFLAGS) $(LDFLAGS) build/main.o -L/opt/devkitpro/libnx/lib -lnx -o $@

build/main.o: main.c
	@mkdir -p build
	$(CC) $(CFLAGS) -c main.c -o $@

clean:
	rm -rf build $(TARGET).nro $(TARGET).elf
