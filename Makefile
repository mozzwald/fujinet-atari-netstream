# Makefile for FujiNetStream Atari

# Mad Assembler
MADS=/usr/local/bin/mads

# Set the location of your cc65 installation
export CC65_HOME = /usr/share/cc65

# cc65 Target System
CC65_TARGET   = atari

BUILD_DIR     = build
ATR_DIR       = $(BUILD_DIR)/atr_root
DIR2ATR       = dir2atr

# Base address for handler-esque binary to exist on Atari
HANDLER_BASE  = 10240

# cc65 toolchain
CC65 ?= cl65
CFLAGS ?= -t $(CC65_TARGET)

# FujiNet Library
FUJINET_LIB_VERSION = 4.9.0
FUJINET_LIB_DIR = fujinet-lib-$(CC65_TARGET)-$(FUJINET_LIB_VERSION)
FUJINET_LIB = $(FUJINET_LIB_DIR)/fujinet-$(CC65_TARGET)-$(FUJINET_LIB_VERSION).lib
FUJINET_INCLUDES = -I$(FUJINET_LIB_DIR)

all: $(ATR_DIR)/NSENGINE.OBX $(ATR_DIR)/autorun.sys $(ATR_DIR)/DOS.SYS $(ATR_DIR)/DUP.SYS $(BUILD_DIR)/linux_netstream_chat $(BUILD_DIR)/atari_netstream_chat.atr

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(ATR_DIR)/NSENGINE.OBX: handler/netstream.s | $(ATR_DIR)
	$(MADS) handler/netstream.s -i:handler/include -d:BASEADDR=$(HANDLER_BASE) -d:HIBUILD=0 -s -p -o:$@

$(ATR_DIR)/autorun.sys: examples/atari_netstream_chat.c examples/netstream_api.s examples/atari_netstream.cfg | $(ATR_DIR)
	$(CC65) $(CFLAGS) -C examples/atari_netstream.cfg -o $@ examples/atari_netstream_chat.c examples/netstream_api.s

$(ATR_DIR)/DOS.SYS: examples/dos/DOS.SYS | $(ATR_DIR)
	cp examples/dos/DOS.SYS $(ATR_DIR)/DOS.SYS

$(ATR_DIR)/DUP.SYS: examples/dos/DUP.SYS | $(ATR_DIR)
	cp examples/dos/DUP.SYS $(ATR_DIR)/DUP.SYS

$(BUILD_DIR)/linux_netstream_chat: examples/linux_netstream_chat.c | $(BUILD_DIR)
	$(CC) -O2 -Wall -Wextra -o $@ examples/linux_netstream_chat.c

$(ATR_DIR): | $(BUILD_DIR)
	mkdir -p $(ATR_DIR)

$(BUILD_DIR)/atari_netstream_chat.atr: $(ATR_DIR)/autorun.sys $(ATR_DIR)/NSENGINE.OBX $(ATR_DIR)/DOS.SYS $(ATR_DIR)/DUP.SYS | $(BUILD_DIR)
	$(DIR2ATR) -b Dos25 720 $@ $(ATR_DIR)

clean:
	rm -rf $(BUILD_DIR)/*

.PHONY: all clean
