# Makefile for FreeSWITCH Subscribe Handler Module
# This builds the custom module for handling SUBSCRIBE messages during transfers

# FreeSWITCH installation directory (adjust as needed)
FREESWITCH_DIR = /usr/local/freeswitch
FREESWITCH_SRC = /usr/src/freeswitch

# Module information
MODULE_NAME = mod_subscribe_handler
MODULE_DIR = $(FREESWITCH_DIR)/mod

# Compiler settings
CC = gcc
CFLAGS = -Wall -Wextra -O2 -g -fPIC -std=c99
INCLUDES = -I$(FREESWITCH_SRC)/src/include \
           -I$(FREESWITCH_SRC)/libs/libteletone/src \
           -I$(FREESWITCH_SRC)/libs/sofia-sip/libsofia-sip-ua/su \
           -I$(FREESWITCH_SRC)/libs/sofia-sip/libsofia-sip-ua/sip \
           -I$(FREESWITCH_SRC)/libs/sofia-sip/libsofia-sip-ua/url \
           -I$(FREESWITCH_SRC)/libs/sofia-sip/libsofia-sip-ua/msg \
           -I$(FREESWITCH_SRC)/libs/sofia-sip/libsofia-sip-ua/nua \
           -I$(FREESWITCH_SRC)/libs/sofia-sip/libsofia-sip-ua/nta \
           -I$(FREESWITCH_SRC)/libs/sofia-sip/libsofia-sip-ua/tport \
           -I$(FREESWITCH_SRC)/libs/sofia-sip/libsofia-sip-ua/sdp \
           -I$(FREESWITCH_SRC)/libs/sofia-sip/libsofia-sip-ua/soa \
           -I$(FREESWITCH_SRC)/libs/esl/src/include

LDFLAGS = -shared -Wl,-soname,$(MODULE_NAME).so
LIBS = -pthread

# Source files
SOURCES = freeswitch_subscribe_handler.c
OBJECTS = $(SOURCES:.c=.o)
TARGET = $(MODULE_NAME).so

# Installation directories
INSTALL_DIR = $(FREESWITCH_DIR)/mod
CONFIG_DIR = $(FREESWITCH_DIR)/conf/autoload_configs

.PHONY: all clean install uninstall test

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

install: $(TARGET)
	@echo "Installing $(MODULE_NAME)..."
	@mkdir -p $(INSTALL_DIR)
	@cp $(TARGET) $(INSTALL_DIR)/
	@echo "Creating module configuration..."
	@echo "<configuration name=\"$(MODULE_NAME).conf\" description=\"Subscribe Handler\">" > $(CONFIG_DIR)/$(MODULE_NAME).conf.xml
	@echo "  <settings>" >> $(CONFIG_DIR)/$(MODULE_NAME).conf.xml
	@echo "    <param name=\"enabled\" value=\"true\"/>" >> $(CONFIG_DIR)/$(MODULE_NAME).conf.xml
	@echo "    <param name=\"log-level\" value=\"info\"/>" >> $(CONFIG_DIR)/$(MODULE_NAME).conf.xml
	@echo "    <param name=\"transfer-timeout\" value=\"200\"/>" >> $(CONFIG_DIR)/$(MODULE_NAME).conf.xml
	@echo "  </settings>" >> $(CONFIG_DIR)/$(MODULE_NAME).conf.xml
	@echo "</configuration>" >> $(CONFIG_DIR)/$(MODULE_NAME).conf.xml
	@echo "Installation complete. Add 'load $(MODULE_NAME)' to modules.conf.xml"

uninstall:
	@echo "Uninstalling $(MODULE_NAME)..."
	@rm -f $(INSTALL_DIR)/$(TARGET)
	@rm -f $(CONFIG_DIR)/$(MODULE_NAME).conf.xml
	@echo "Uninstallation complete."

clean:
	rm -f $(OBJECTS) $(TARGET)

test: $(TARGET)
	@echo "Testing $(MODULE_NAME)..."
	@echo "Use 'fs_cli -x \"load $(MODULE_NAME)\"' to load the module"
	@echo "Use 'fs_cli -x \"subscribe_status <call-id>\"' to test the API"

# Build sofia patch
sofia-patch: sofia_sip_subscribe_patch.c
	@echo "Building Sofia SIP patch..."
	@echo "This patch should be integrated into FreeSWITCH source code."
	@echo "Copy the contents of sofia_sip_subscribe_patch.c to mod_sofia/sofia.c"

# Install dialplan configurations
install-dialplan:
	@echo "Installing dialplan configurations..."
	@cp freeswitch_transfer_dialplan.xml $(FREESWITCH_DIR)/conf/dialplan/
	@echo "Dialplan configurations installed."

# Development targets
dev-install: all install install-dialplan

debug: CFLAGS += -DDEBUG -g3
debug: all

# Help target
help:
	@echo "Available targets:"
	@echo "  all           - Build the module"
	@echo "  install       - Install the module to FreeSWITCH"
	@echo "  uninstall     - Remove the module from FreeSWITCH"
	@echo "  clean         - Remove build artifacts"
	@echo "  test          - Show testing instructions"
	@echo "  sofia-patch   - Show Sofia SIP patch instructions"
	@echo "  install-dialplan - Install dialplan configurations"
	@echo "  dev-install   - Build and install everything"
	@echo "  debug         - Build with debug symbols"
	@echo "  help          - Show this help"

# Variables for easy customization
print-vars:
	@echo "FREESWITCH_DIR = $(FREESWITCH_DIR)"
	@echo "FREESWITCH_SRC = $(FREESWITCH_SRC)"
	@echo "MODULE_NAME = $(MODULE_NAME)"
	@echo "CC = $(CC)"
	@echo "CFLAGS = $(CFLAGS)"
	@echo "INCLUDES = $(INCLUDES)"