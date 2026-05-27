# ── M1Switch E2E Test NRO — 共享构建规则 ──────────────────────
# 每个测试子目录的 Makefile 只需设置 TARGET 和 SOURCES，
# 然后 include 本文件。
#
# 用法:
#   TARGET := my_test
#   SOURCES := source
#   include $(DEVKITPRO)/libnx/switch_rules
#   include ../common.mk
#───────────────────────────────────────────────────────────────

# ── 架构标志 ──────────────────────────────────────────────────
ARCH	:=	-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

# ── 编译标志 ──────────────────────────────────────────────────
CFLAGS	:=	-g -Wall -O2 -ffunction-sections \
			$(ARCH) $(DEFINES)
CFLAGS	+=	$(INCLUDE) -D__SWITCH__

CXXFLAGS:=	$(CFLAGS) -fno-rtti -fno-exceptions
ASFLAGS	:=	-g $(ARCH)
LDFLAGS	:=	-specs=$(DEVKITPRO)/libnx/switch.specs \
			-g $(ARCH) \
			-Wl,-Map,$(notdir $*.map)

LIBS	:=	-lnx

LIBDIRS	:=	$(PORTLIBS) $(LIBNX)

# ── 源文件扫描 ────────────────────────────────────────────────
CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s))) \
			$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.S)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)
export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
					$(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR	:=	$(CURDIR)/$(BUILD)

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SRC	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o) $(SFILES:.S=.o)
export OFILES 		:=	$(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN	:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
					$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
					-I$(CURDIR)/$(BUILD)
export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

# ── 链接器选择 ────────────────────────────────────────────────
ifeq ($(strip $(CPPFILES)),)
	export LD := $(CC)
else
	export LD := $(CXX)
endif

# ── 禁用 NACP/ICON（不需要）───────────────────────────────────
NO_ICON := 1
NO_NACP := 1

# ── 构建目标 ──────────────────────────────────────────────────
.PHONY: $(BUILD) clean all

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).elf $(TARGET).map

# ── 在 $(BUILD) 目录内的递归构建 ──────────────────────────────
ifeq ($(CURDIR),$(TOPDIR))

# 第一遍：只设置变量并触发递归
# (上面的 all 规则会处理)

else

# 第二遍：真正编译
DEPENDS	:=	$(OFILES:.o=.d)

all: $(OUTPUT).nro

$(OUTPUT).nro: $(OUTPUT).elf

$(OUTPUT).elf: $(OFILES)

%.bin.o %_bin.h : %.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

endif
