# PNG Image Viewer & MP4 Exporter

A fast, multi-threaded PNG image sequence viewer for Windows and linux with high-quality MP4 export capabilities. Designed for viewing and exporting large image sequences.

## Features

- **Fast preview**: Load thousands of images with configurable shrink factor
- **Multi-threaded loading**: Parallel image loading for quick startup
- **Zoom & pan**: Mouse wheel to zoom, drag to pan
- **Animation playback**: Play through sequences with real-time FPS display
- **High-quality MP4 export**: Exports using original full-resolution files (Windows)
- **Multi-threaded export**: Parallel rendering for fast exports (Windows)
- **Memory efficient**: Only keeps preview images in RAM; exports read originals on-the-fly

## Quick Start

### Windows

```bash
# Compile with g++ (MinGW)
g++ -o display_image.exe display_image.cpp -lgdi32 -lcomdlg32 -lole32 -O2 -std=c++11

# Run
display_image.exe
```

### Linux

```bash
git clone https://github.com/FilipO28555/png_viewer_renderer.git
cd png_viewer_renderer/linux
module load SDL2
make deps
make
./display_image -f /path/to/images
```

See `linux/README.md` for more installation options if SDL2 module is not available.

## Command Line Options

| Option | Description | Default |
|--------|-------------|---------|
| `-f, --folder <path>` | Folder containing images (Linux only, required) | - |
| `-s, --shrink <factor>` | Shrink factor for preview images (integer)| Auto |
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
| MP4 export | ✅ Full support | ✅ Full support |
| File dialogs | Native | Command line |

On Linux the output of the encoder is the folder from where you start the program.

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

**During export:**
- One source image loaded at a time per thread

## License

This project is provided as-is for personal and academic use.

## Acknowledgments

- [stb](https://github.com/nothings/stb) - Single-file public domain libraries by Sean Barrett
- [SDL2](https://www.libsdl.org/) - Cross-platform multimedia library
- [FFmpeg](https://ffmpeg.org/) - Complete, cross-platform solution for video processing
