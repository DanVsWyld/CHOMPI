# Project Name
TARGET = CHOMPI

DEBUG=0
# OPT=-O0
OPT=-O3

APP_TYPE=BOOT_SRAM
# make program-boot installs CHOMPI's own bootloader, not libDaisy's generic one
BOOT_BIN = ../../../chompi-wave/bin/CHOMPI_Bootloader_V6_2_0.bin
LDSCRIPT = chompi_sram.lds

# This firmware borrows WAVE's vendored libraries (including CHOMPI's libDaisy
# changes for DMA MIDI and the TIM16 clock) instead of carrying a fourth copy.
LIBS_DIR = ../../../chompi-wave/code/libs

# FreeRTOS coreJSON
C_SOURCES = $(LIBS_DIR)/coreJSON/source/core_json.c
C_INCLUDES +=  -I$(LIBS_DIR)/coreJSON/source/include

# Sources
CPP_SOURCES = chompi_main.cpp \
	encoder.cpp

USE_FATFS = 1

# Libs
LIBDAISY_DIR = $(LIBS_DIR)/libDaisy
DAISYSP_DIR = $(LIBS_DIR)/DaisySP

# Core location, and generic Makefile
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
