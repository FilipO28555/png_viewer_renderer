# PNG Image Viewer & MP4 Exporter

A fast, multi-threaded PNG image sequence viewer for Windows with high-quality MP4 export capabilities. Designed for viewing and exporting large image sequences (e.g., simulation outputs).

## Features

- **Fast preview**: Load thousands of images with configurable shrink factor
- **Multi-threaded loading**: Parallel image loading for quick startup
- **Zoom & pan**: Mouse wheel to zoom, drag to pan
- **Animation playback**: Play through sequences with real-time FPS display
- **High-quality MP4 export**: Exports using original full-resolution files
- **Multi-threaded export**: Parallel rendering for fast exports
- **Memory efficient**: Only keeps preview images in RAM; exports read originals on-the-fly

## Requirements

- Windows OS
- [stb_image.h](https://github.com/nothings/stb) - Single-file image loading library
- [FFmpeg](https://ffmpeg.org/) - Required for MP4 export (must be in PATH)
- C++11 compatible compiler (g++, MSVC, etc.)

## Compilation

### 1. Download stb_image.h

Download `stb_image.h` from the [stb repository](https://github.com/nothings/stb/blob/master/stb_image.h) and place it in the same directory as `display_image.cpp`.

```bash
curl -O https://raw.githubusercontent.com/nothings/stb/master/stb_image.h
```

### 2. Compile with g++ (MinGW)

```bash
g++ -o display_image.exe display_image.cpp -lgdi32 -lcomdlg32 -lole32 -O2 -std=c++11
```

### 3. Compile with MSVC

```bash
cl /O2 /EHsc display_image.cpp gdi32.lib comdlg32.lib ole32.lib shell32.lib
```

## Usage

```bash
display_image.exe [options]
```

### Command Line Options

| Option | Description | Default |
|--------|-------------|---------|
| `-s, --shrink <factor>` | Shrink factor for preview images | Auto (image ~2× window size) |
| `-n, --nth <n>` | Load every n-th image for preview | 1 (all images) |
| `-x <width>` | Window width in pixels | 1000 |
| `-y <height>` | Window height in pixels | 1000 |
| `-t, --threads <n>` | Number of threads for loading/export | 12 |
| `-h, --help` | Show help message | - |

### Examples

```bash
# Default settings (auto shrink, 1000x1000 window)
display_image.exe

# Large window with 8 threads
display_image.exe -x 1920 -y 1080 -t 8

# Load every 10th image for quick preview
display_image.exe -n 10

# Force specific shrink factor
display_image.exe -s 4

# Combine options
display_image.exe -x 1280 -y 720 -n 5 -t 16 -s 2
```

## Controls

| Key/Action | Function |
|------------|----------|
| **Left/Right Arrow** or **A/D** | Previous/Next image |
| **Home** | First image |
| **End** | Last image |
| **Space** | Play/Pause animation |
| **J** | Reverse playback direction |
| **S** | Export current view to MP4 |
| **E** | Export view settings to file |
| **Mouse Wheel** | Zoom in/out |
| **Left Mouse Drag** | Pan |
| **R** | Reset zoom/pan |
| **ESC** | Change folder |
| **Q** | Quit |

## File Naming Convention

Images must match the pattern: `*_<number>.png`

Examples of valid filenames:
- `frame_000001.png`
- `image_123.png`
- `e_png_yx_0.5_015000.png`
- `simulation_output_42.png`

Images are automatically sorted by their numeric suffix.

## Export Features

### High-Quality MP4 Export (Press S)

- Exports **all files** in the folder (not just the loaded preview subset)
- Uses **original full-resolution** source files
- Renders at the **current zoom/pan view**
- Multi-threaded for fast processing
- Output resolution matches window size (`-x` and `-y` options)

This means you can:
1. Load with `-n 10 -s 8` for quick preview (low memory)
2. Navigate and zoom into an interesting region
3. Press **S** to export all frames at full quality

### Memory Usage

**During preview:**
- Only shrunk preview images are kept in RAM
- Example: 1000 images at 2000×2000 preview = ~12 GB RAM

**During export:**
- One source image loaded at a time per thread
- Bounded queue prevents memory explosion
- Example with 12 threads, 4000×4000 source: ~650 MB peak

## License

This project is provided as-is for personal and academic use.

## Acknowledgments

- [stb](https://github.com/nothings/stb) - Single-file public domain libraries by Sean Barrett
- [FFmpeg](https://ffmpeg.org/) - Complete, cross-platform solution for video processing
