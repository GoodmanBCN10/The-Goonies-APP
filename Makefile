#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= .
include $(DEVKITPRO)/libnx/switch_rules

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & dependancy files will be placed
# SOURCES is a list of directories containing source code
# DATA is a list of directories containing data files
# INCLUDES is a list of directories containing header files
# EXEFS_SRC is a list of directories containing data files and binaries for the exefs
# ROMFS is the directory containing data to be added to RomFS, relative to the makefile
#
# NO_ICON: if set to anything, do not use icon.
# NO_NACP: if set to anything, no .nacp file is generated.
# APP_TITLE is the name of the app stored in the .nacp file (Optional)
# APP_AUTHOR is the author of the app stored in the .nacp file (Optional)
# APP_VERSION is the version of the app stored in the .nacp file (Optional)
# APP_TITLEID is the titleID of the app stored in the .nacp file (Optional)
# ICON is the filename of the icon (.jpg), relative to the project folder.
#   If not set, it attempts to use one of the following (in this order):
#     - <Project name>.jpg
#     - icon.jpg
#     - <libnx folder>/default_icon.jpg
#---------------------------------------------------------------------------------
TARGET		:=	TheGooniesInstaller
BUILD		:=	build
SOURCES		:=	vendor/qrcodegen vendor/dht vendor/libnx-ext/libnx-ext/source vendor/libnx-ext/libnx-ipcext/source source source/app source/ui source/ui/catalog source/ui/downloads source/ui/forwarders source/ui/installed source/ui/mtp source/ui/saves source/ui/settings source/ui/updater source/ui/detail source/install source/platform  source/installer source/utils source/yati source/yati/container source/yati/nx source/yati/nx/nxdumptool source/yati/source source/minini source/libhaze source/mtp source/core source/cheats
DATA		:=	data
INCLUDES	:=	vendor source vendor/libnx-ext/libnx-ext/include vendor/libnx-ext/libnx-ipcext/include include include/minini include/yati/nx/nxdumptool include/libhaze vendor/borealis/library/include vendor/borealis/library/include/borealis/extern vendor/borealis/library/include/borealis/extern/nanovg vendor/borealis/library/lib/extern/nanovg vendor/borealis/library/lib/extern/fmt/include vendor/borealis/library/lib/extern/yoga vendor/borealis/library/lib/extern/tweeny/include
ROMFS		:=	romfs
LIBDIRS		:=	$(PORTLIBS) $(LIBNX)

INCLUDE		:=	$(foreach dir,$(INCLUDES),-I$(TOPDIR)/$(dir)) \
				$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
				-I.

APP_TITLE	:=	The Goonies App 3.0.2
APP_AUTHOR	:=	GoodmanBCN
APP_VERSION	:=	v3.0.2
DEFINES		:=	-DTHEGOONIES_VERSION=\"$(APP_VERSION)\"

#---------------------------------------------------------------------------------
ARCH	:=	-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

# Ensure we use portlibs pkg-config
export PKG_CONFIG_PATH := $(PORTLIBS)/lib/pkgconfig

CFLAGS	:=	-Wall -Os -ffunction-sections -fdata-sections \
			$(ARCH) $(DEFINES)

# We use pkg-config to get SDL2 flags
CFLAGS	+=	$(INCLUDE) -D__SWITCH__ `pkg-config --cflags sdl2 SDL2_image SDL2_ttf SDL2_mixer`

CXXFLAGS	:= $(CFLAGS) -std=gnu++23 -DBRLS_RESOURCES=\"romfs:/\"

ASFLAGS	:=	$(ARCH)
LDFLAGS	=	-specs=$(DEVKITPRO)/libnx/switch.specs $(ARCH) -Wl,--gc-sections -s -Wl,-Map,$(notdir $*.map)

LIBS	:=	$(TOPDIR)/vendor/borealis/library/libborealis.a $(TOPDIR)/vendor/borealis/library/lib/extern/fmt/libfmt.a $(TOPDIR)/vendor/borealis/library/lib/extern/yoga/yoga/libyogacore.a $(TOPDIR)/vendor/borealis/library/libtinyxml2.a `pkg-config --libs --static sdl2 SDL2_image SDL2_ttf SDL2_gfx SDL2_mixer` -lmbedtls -lmbedcrypto -lmbedx509 -lzstd -lusbhsfs -lntfs-3g -llwext4 -lcurl -lminizip -lz -ldeko3d -lminiupnpc -lnx -lm

#---------------------------------------------------------------------------------
ifeq ($(TOPDIR),..)
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(TARGET)

export VPATH	:=	$(foreach dir,$(SOURCES),../$(dir)) \
			$(foreach dir,$(DATA),../$(dir))

export DEPSDIR	:=	.

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard ../$(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard ../$(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard ../$(dir)/*.s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard ../$(dir)/*.*)))

ifeq ($(strip $(CPPFILES)),)
	export LD	:=	$(CC)
else
	export LD	:=	$(CXX)
endif

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SRC	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES 	:=	$(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN	:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export APP_ICON := ../icon.jpg

export NROFLAGS += --icon=$(APP_ICON) --nacp=$(TARGET).nacp

ifneq ($(ROMFS),)
	export NROFLAGS += --romfsdir=../$(ROMFS)
endif

.PHONY:	all

DEPENDS	:=	$(OFILES:.o=.d)

all	:	$(OUTPUT).pfs0 $(OUTPUT).nro
	@cp -f $(OUTPUT).nro ../$(TARGET).nro
	@echo "Built $(TARGET).nro successfully!"

$(OUTPUT).pfs0	:	$(OUTPUT).nso
$(OUTPUT).nso	:	$(OUTPUT).elf
$(OUTPUT).nro	:	$(OUTPUT).elf $(OUTPUT).nacp
$(OUTPUT).elf	:	$(OFILES)

$(OFILES_SRC)	: $(HFILES_BIN) ../Makefile

%.bin.o	%_bin.h :	%.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------

.PHONY: $(BUILD) clean all

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f ../Makefile TOPDIR=..

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).pfs0 $(TARGET).nso $(TARGET).nro $(TARGET).nacp $(TARGET).elf

endif
#---------------------------------------------------------------------------------

