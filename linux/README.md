# PNG Image Viewer - Linux Version

SDL2-based PNG image sequence viewer for Linux with 2D and 3D (z-slice) navigation, MP4 export, and multi-threaded loading.

## Features

- **Multi-threaded loading**: Fast parallel image loading (configurable thread count)
- **Zoom & pan**: Mouse wheel to zoom, drag to pan
- **Animation playback**: Play/pause sequences with FPS display
- **MP4 export**: Export current view (with zoom/pan) to MP4 via ffmpeg
- **3D mode**: Navigate z-height slices with Shift+Wheel or Up/Down arrows
- **Memory efficient**: Configurable shrink factor for previews; all z-heights preloaded for instant switching

## Requirements

- SDL2 development libraries
- g++ with C++17 support
- pthread
- ffmpeg (for MP4 export)
- stb_image.h (included in `../common/`)

## Installation

### Quick Install (copy & paste)

```bash
git clone https://github.com/FilipO28555/png_viewer_renderer.git
cd png_viewer_renderer/linux
sudo apt install libsdl2-dev
make deps
make
```

### HPC/Cluster Systems (no sudo access)

On shared systems, SDL2 may be available via the module system:

```bash
# Check available modules
module avail sdl
module avail SDL

# Load SDL2 if available (name varies by system)
module load sdl2
# or
module load SDL2

# Then build
make deps
make
```

If SDL2 is not available as a module, you can install it locally:

```bash
cd ~
wget https://github.com/libsdl-org/SDL/releases/download/release-2.30.0/SDL2-2.30.0.tar.gz
tar -xzf SDL2-2.30.0.tar.gz
cd SDL2-2.30.0
./configure --prefix=$HOME/.local
make -j$(nproc)
make install

# Add to your environment (add to ~/.bashrc for persistence)
export PATH="$HOME/.local/bin:$PATH"
export LD_LIBRARY_PATH="$HOME/.local/lib:$LD_LIBRARY_PATH"
export PKG_CONFIG_PATH="$HOME/.local/lib/pkgconfig:$PKG_CONFIG_PATH"

cd /path/to/png_viewer_renderer/linux
make deps
make
```

### Manual Installation

#### 1. Install SDL2

```bash
# Debian/Ubuntu
sudo apt install libsdl2-dev

# Fedora
sudo dnf install SDL2-devel

# Arch Linux
sudo pacman -S sdl2

# CentOS/RHEL
sudo yum install SDL2-devel
```

#### 2. Get stb_image.h

```bash
make deps
```

#### 3. Build

```bash
make
```

## Usage

```bash
./display_image -f /path/to/images [options]
```

### Options

| Option | Description | Default |
|--------|-------------|---------|
| `-f, --folder <path>` | Folder containing images (required) | - |
| `--3d, --3D` | 3D mode: folder contains `z<number>` subfolders | off |
| `-s, --shrink <factor>` | Shrink factor for preview | Auto |
| `-n, --nth <n>` | Load every n-th image | 1 |
| `-x <width>` | Window width in pixels | 1000 |
| `-y <height>` | Window height in pixels | 1000 |
| `-t, --threads <n>` | Number of threads (1–128) | 72 |
| `--debug` | Show verbose debug output | off |
| `-h, --help` | Show help and exit | - |

### Examples

```bash
# Basic usage
./display_image -f /path/to/simulation/output

# 3D mode (folder contains z0001/, z0002/, ... subfolders)
./display_image -f /path/to/3d_data --3d

# Large window, custom thread count
./display_image -f ./images -x 1920 -y 1080 -t 8

# Quick preview (every 10th image)
./display_image -f ./images -n 10

# Force shrink factor
./display_image -f ./images -s 4
```

## Controls

| Key / Action | Function |
|---|---|
| Left / Right Arrow, A / D | Previous / Next frame |
| Home / End | First / Last frame |
| Space | Play / Pause |
| J | Reverse playback direction |
| Mouse Wheel | Zoom in / out |
| Left Mouse Drag | Pan |
| R | Reset zoom and pan |
| S | Export current view to MP4 |
| Up / Down Arrow | Change z-height *(3D mode)* |
| Shift + Mouse Wheel | Change z-height *(3D mode)* |
| Q / Escape | Quit |

## 3D Mode

When your data is organised as z-slice subfolders, use `--3d`:

```
/path/to/data/
  z0100/   image_000001.png  image_000002.png  ...
  z0200/   image_000001.png  image_000002.png  ...
  z0300/   ...
```

```bash
./display_image -f /path/to/data --3d
```

All z-heights are loaded into RAM at startup for instant switching. The window title shows the current z-height (`[Z:200]`).

> **Memory note**: with many z-heights and large images, RAM usage can be significant (e.g. 15 z-heights × 241 frames × 2600×2000 px ≈ 47 GB). Use `-s` to reduce it:
> ```bash
> ./display_image -f /path/to/data --3d -s 2   # quarter the RAM usage
> ```

## File Naming Convention

Images must match the pattern `*_<number>.png` and are sorted by numeric suffix.

Examples: `frame_000001.png`, `e_png_yx_0.5_015000.png`

## Troubleshooting

### "SDL initialization failed"
Make sure SDL2 is installed and an X11/Wayland display is available.

### "Could not open directory"
Check that the folder path exists and you have read permissions.

### "No z-folders found" (3D mode)
Subfolders must be named `z<number>` (e.g. `z0100`, `z0200`).

### Black window
Verify that images exist and match the `*_<number>.png` pattern.
