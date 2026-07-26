CC ?= cc
PKG_CONFIG ?= pkg-config

BUILD_DIR := build
COMMON_SOURCES := src/protocol.c
COMMON_HEADERS := $(wildcard include/*.h)
CATALOG_SOURCES := src/catalog.c src/catalog_generated.c
CATALOG_GENERATED := include/roadcast_catalog_generated.h include/roadcast_frames.h src/catalog_generated.c
LIBUV_CFLAGS := $(shell $(PKG_CONFIG) --cflags libuv 2>/dev/null)
LIBUV_LIBS := $(shell $(PKG_CONFIG) --libs libuv 2>/dev/null)
LIBELF_PREFIX := $(shell brew --prefix libelf 2>/dev/null)
LIBELF_CFLAGS := $(if $(LIBELF_PREFIX),-I$(LIBELF_PREFIX)/include)

CPPFLAGS := -Iinclude
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Werror
LDFLAGS ?=

.PHONY: all android clean generate test integration

all: $(BUILD_DIR)/roadcastd $(BUILD_DIR)/roadcastctl

$(BUILD_DIR):
	mkdir -p $@

$(CATALOG_GENERATED): data/dbc.json scripts/generate-catalog.py
	scripts/generate-catalog.py

generate:
	scripts/generate-catalog.py

$(BUILD_DIR)/roadcastd: src/roadcastd.c src/vhal_source.c $(COMMON_SOURCES) $(CATALOG_SOURCES) $(COMMON_HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(LIBUV_CFLAGS) $(LIBELF_CFLAGS) $(CFLAGS) \
		$(LDFLAGS) -o $@ src/roadcastd.c src/vhal_source.c \
		$(COMMON_SOURCES) $(CATALOG_SOURCES) $(LIBUV_LIBS) -pthread

$(BUILD_DIR)/roadcastctl: src/roadcastctl.c $(COMMON_SOURCES) $(COMMON_HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		src/roadcastctl.c $(COMMON_SOURCES)

$(BUILD_DIR)/test_protocol: tests/test_protocol.c $(COMMON_SOURCES) $(COMMON_HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		tests/test_protocol.c $(COMMON_SOURCES)

$(BUILD_DIR)/test_catalog: tests/test_catalog.c $(COMMON_SOURCES) $(CATALOG_SOURCES) $(COMMON_HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		tests/test_catalog.c $(COMMON_SOURCES) $(CATALOG_SOURCES)

test: $(BUILD_DIR)/test_protocol $(BUILD_DIR)/test_catalog
	$(BUILD_DIR)/test_protocol
	$(BUILD_DIR)/test_catalog

integration: all
	tests/integration.sh

android:
	scripts/build-android.sh

clean:
	rm -rf $(BUILD_DIR)
