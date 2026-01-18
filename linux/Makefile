# Compiler
CXX = g++
CXXFLAGS = -O2 -std=c++17 -Wall $(shell sdl2-config --cflags)
LDFLAGS = $(shell sdl2-config --libs) -lpthread

# Source files
COMMON_DIR = ../common
SRCS = display_image_linux.cpp $(COMMON_DIR)/image_loader.cpp
OBJS = display_image_linux.o image_loader.o

# Output
TARGET = display_image

# Default target
all: $(TARGET)

# Link
$(TARGET): $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

# Compile main
display_image_linux.o: display_image_linux.cpp $(COMMON_DIR)/frame_types.h $(COMMON_DIR)/math_utils.h $(COMMON_DIR)/image_loader.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Compile image_loader
image_loader.o: $(COMMON_DIR)/image_loader.cpp $(COMMON_DIR)/image_loader.h $(COMMON_DIR)/stb_image.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Clean
clean:
	rm -f $(TARGET) $(OBJS)

# Install stb_image.h if not present
deps:
	@if [ ! -f $(COMMON_DIR)/stb_image.h ]; then \
		echo "Downloading stb_image.h..."; \
		curl -o $(COMMON_DIR)/stb_image.h https://raw.githubusercontent.com/nothings/stb/master/stb_image.h; \
	else \
		echo "stb_image.h already exists"; \
	fi

# Help
help:
	@echo "PNG Image Viewer - Linux Build"
	@echo ""
	@echo "Targets:"
	@echo "  all    - Build the program (default)"
	@echo "  clean  - Remove built files"
	@echo "  deps   - Download stb_image.h if missing"
	@echo "  help   - Show this help"
	@echo ""
	@echo "Requirements:"
	@echo "  - SDL2 development libraries (libsdl2-dev)"
	@echo "  - g++ with C++17 support"
	@echo "  - stb_image.h in ../common/"
	@echo ""
	@echo "Install dependencies:"
	@echo "  Debian/Ubuntu: sudo apt install libsdl2-dev"
	@echo "  Fedora:        sudo dnf install SDL2-devel"
	@echo "  Arch:          sudo pacman -S sdl2"

.PHONY: all clean deps help
