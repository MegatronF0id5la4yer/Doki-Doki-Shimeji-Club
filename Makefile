CXX := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wno-deprecated-declarations -Iinclude
PKG_CFLAGS := $(shell pkg-config --cflags gtk+-3.0 gtk-layer-shell-0 librsvg-2.0 pangocairo)
PKG_LIBS := $(shell pkg-config --libs gtk+-3.0 gtk-layer-shell-0 librsvg-2.0 pangocairo)

RUST_DIR := rust_core
RUST_LIB := $(RUST_DIR)/target/release/libnatsuki_core.so
TARGET := natsuki-shimeji

.PHONY: all rust python test clean

all: rust $(TARGET)

rust:
	cd $(RUST_DIR) && cargo build --release

$(TARGET): cpp_ui/main.cpp cpp_ui/svg_icons.hpp cpp_ui/brain_client.hpp cpp_ui/audio_reactive.hpp include/natsuki_core.h $(RUST_LIB)
	$(CXX) $(CXXFLAGS) $(PKG_CFLAGS) cpp_ui/main.cpp -o $(TARGET) \
		-L$(RUST_DIR)/target/release -Wl,-rpath,'$$ORIGIN/$(RUST_DIR)/target/release' -lnatsuki_core \
		$(PKG_LIBS)

test:
	cd $(RUST_DIR) && cargo test --release
	python3 -m py_compile brain/natsuki_brain.py brain/voice_commands.py
	$(shell if [ -x .venv/bin/python3 ]; then echo .venv/bin/python3; else echo python3; fi) brain/natsuki_brain.py --self-test

clean:
	rm -f $(TARGET)
	cd $(RUST_DIR) && cargo clean

