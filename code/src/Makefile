# PHYMO: an 8-voice physical-modelling synth for the CHOMPI.
#
# Build with Arm GNU Embedded Toolchain 10.3-2021.10. Build libDaisy once first:
#   make -C code/libs/libDaisy
# Output: build/CHOMPI.bin

TARGET = CHOMPI

DEBUG = 0
OPT = -O3

# runs from SRAM, loaded there by the CHOMPI bootloader
APP_TYPE = BOOT_SRAM
LDSCRIPT = chompi_sram.lds

CPP_SOURCES = chompi_main.cpp \
	encoder.cpp

# the SD card (presets)
USE_FATFS = 1

LIBDAISY_DIR = ../libs/libDaisy

# hot loops in the ITCM (PHYMO_HOT); `make ITCM=0` builds without
ITCM ?= 1
ifeq ($(ITCM),1)
C_DEFS = -DPHYMO_ITCM
endif
# sqrtf and the like as instructions, without errno
C_DEFS += -fno-math-errno
# the string delay lines are provided by chompi_main.cpp
C_DEFS += -DPHYMO_STRINGS_OUTSIDE
# the reverb: `make REVERB=0` full rate, 1 half rate (cheapest), 2 half
# rate with steeper filters (default)
REVERB ?= 2
C_DEFS += -DPHYMO_REVERB=$(REVERB)
# diagnostics: `make DIAG=1` adds the CPU meter, overload flash,
# PROFILE.txt and CRASH.txt (development builds only)
DIAG ?= 0
ifeq ($(DIAG),1)
C_DEFS += -DPHYMO_DIAG
endif

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
