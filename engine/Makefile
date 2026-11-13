# prayog engine Makefile — aarch64 Termux + Android NDK-friendly.
#
#   make                  build build/prayog
#   make install          copy to ~/.local/bin or $PREFIX/bin
#   make clean

CXX      ?= clang++
CXXSTD   := -std=c++17
WARN     := -Wall -Wextra -Wno-unused-parameter
OPT      := -O2
INCLUDE  := -Iinclude

ARCH := $(shell uname -m)
ifeq ($(ARCH),aarch64)
  ARCHFLAGS ?= -march=armv8-a
else
  ARCHFLAGS ?=
endif

CXXFLAGS := $(CXXSTD) $(WARN) $(OPT) $(ARCHFLAGS) $(INCLUDE)

# Termux clang targets Android bionic; no libpthread. On other Linux hosts we
# link it explicitly so pthread symbols resolve on older glibc stacks.
LDLIBS :=
ifneq ($(shell uname -s | tr A-Z a-z),linux)
  LDLIBS += -lpthread
else ifneq (,$(findstring android,$(shell $(CXX) -v 2>&1 | head -1)))
  # Android bionic: libc has pthreads, no -lpthread.
else
  LDLIBS += -lpthread
endif

BUILD := build
SRC   := src/prayog.cpp
BIN   := $(BUILD)/prayog

.PHONY: all clean install

all: $(BIN)

$(BIN): $(SRC) | $(BUILD)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDLIBS)

$(BUILD):
	mkdir -p $(BUILD)

PREFIX_DIR ?= $(HOME)/.local/bin
install: $(BIN)
	mkdir -p $(PREFIX_DIR)
	cp $(BIN) $(PREFIX_DIR)/prayog
	@echo "installed: $(PREFIX_DIR)/prayog"

clean:
	rm -rf $(BUILD)
