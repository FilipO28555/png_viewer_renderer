# PNG Image Viewer & MP4 Exporter

A fast, multi-threaded PNG image sequence viewer with high-quality MP4 export capabilities. Designed for viewing and exporting large image sequences (e.g., simulation outputs).

**Available for Windows and Linux.**

## Features

- **Fast preview**: Load thousands of images with configurable shrink factor
- **Multi-threaded loading**: Parallel image loading for quick startup
- **Zoom & pan**: Mouse wheel to zoom, drag to pan
- **Animation playback**: Play through sequences with real-time FPS display
- **High-quality MP4 export**: Exports using original full-resolution files (Windows)
- **Multi-threaded export**: Parallel rendering for fast exports (Windows)
- **Memory efficient**: Only keeps preview images in RAM; exports read originals on-the-fly

## Repository Structure

```
png_viewer_renderer/
├── common/                    # Shared platform-independent code
│   ├── stb_image.h           # Image loading library
│   ├── frame_types.h         # Common data structures
│   ├── math_utils.h          # Zoom/pan calculations
│   ├── image_loader.h        # Image loading interface
│   └── image_loader.cpp      # Image loading implementation
├── linux/                     # Linux-specific code
│   ├── display_image_linux.cpp
│   ├── Makefile
│   └── README.md
├── display_image.cpp          # Windows GDI implementation
├── README.md                  # This file
└── PORTING_GUIDE_LINUX.md    # Original porting notes
```

## Quick Start

### Windows

```bash
# Compile with g++ (MinGW)
g++ -o display_image.exe display_image.cpp -lgdi32 -lcomdlg32 -lole32 -O2 -std=c++11

# Or with MSVC
cl /O2 /EHsc display_image.cpp gdi32.lib comdlg32.lib ole32.lib shell32.lib

# Run
display_image.exe
```

### Linux

```bash
cd linux

# Install SDL2
sudo apt install libsdl2-dev  # Debian/Ubuntu

# Build
make

# Run
./display_image -f /path/to/images
```

## Command Line Options

| Option | Description | Default |
|--------|-------------|---------|
| `-f, --folder <path>` | Folder containing images (Linux only, required) | - |
| `-s, --shrink <factor>` | Shrink factor for preview images | Auto |
| `-n, --nth <n>` | Load every n-th image for preview | 1 |
| `-x <width>` | Window width in pixels | 1000 |
| `-y <height>` | Window height in pixels | 1000 |
| `-t, --threads <n>` | Number of threads for loading/export | 12 |
| `-h, --help` | Show help message | - |

## Controls

| Key/Action | Function |
|------------|----------|
| **Left/Right Arrow** or **A/D** | Previous/Next image |
| **Home** | First image |
| **End** | Last image |
| **Space** | Play/Pause animation |
| **J** | Reverse playback direction |
| **S** | Export current view to MP4 (Windows) |
| **Mouse Wheel** | Zoom in/out |
| **Left Mouse Drag** | Pan |
| **R** | Reset zoom/pan |
| **ESC** | Change folder (Windows) / Quit (Linux) |
| **Q** | Quit |

## File Naming Convention

Images must match the pattern: `*_<number>.png`

Examples of valid filenames:
- `frame_000001.png`
- `image_123.png`
- `e_png_yx_0.5_015000.png`
- `simulation_output_42.png`

Images are automatically sorted by their numeric suffix.

## Platform Comparison

| Feature | Windows | Linux |
|---------|---------|-------|
| Graphics | GDI | SDL2 |
| Folder selection | GUI dialog | Command line `-f` |
| MP4 export | ✅ Full support | 🔜 Planned |
| File dialogs | Native | Command line |

## Requirements

### Windows
- [stb_image.h](https://github.com/nothings/stb) - Single-file image loading library
- [FFmpeg](https://ffmpeg.org/) - Required for MP4 export (must be in PATH)
- C++11 compatible compiler (g++, MSVC, etc.)

### Linux
- SDL2 development libraries (`libsdl2-dev`)
- g++ with C++17 support
- stb_image.h (included in `common/`)
- FFmpeg (for future MP4 export)

## Memory Usage

**During preview:**
- Only shrunk preview images are kept in RAM
- Auto-shrink targets ~2× window size for preview images

**During export (Windows):**
- One source image loaded at a time per thread
- Bounded queue prevents memory explosion

## License

This project is provided as-is for personal and academic use.

## Acknowledgments

- [stb](https://github.com/nothings/stb) - Single-file public domain libraries by Sean Barrett
- [SDL2](https://www.libsdl.org/) - Cross-platform multimedia library
- [FFmpeg](https://ffmpeg.org/) - Complete, cross-platform solution for video processing
