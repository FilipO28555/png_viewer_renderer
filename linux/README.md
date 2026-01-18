# PNG Image Viewer - Linux Version

SDL2-based PNG image sequence viewer for Linux, ported from the Windows GDI version.

## Features

- **Multi-threaded loading**: Fast parallel image loading
- **Zoom & pan**: Mouse wheel to zoom, drag to pan
- **Animation playback**: Play sequences with FPS display
- **Memory efficient**: Configurable shrink factor for previews

## Requirements

- SDL2 development libraries
- g++ with C++17 support
- pthread
- stb_image.h (included in `../common/`)

## Installation

### Quick Install (copy & paste)

```bash
# Clone the repository and build
git clone https://github.com/FilipO28555/png_viewer_renderer.git
cd png_viewer_renderer/linux
sudo apt install libsdl2-dev
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
```

#### 2. Get stb_image.h

If not already present in `../common/`:

```bash
make deps
```

Or manually:
```bash
curl -o ../common/stb_image.h https://raw.githubusercontent.com/nothings/stb/master/stb_image.h
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
| `-s, --shrink <factor>` | Shrink factor for preview | Auto |
| `-n, --nth <n>` | Load every n-th image | 1 |
| `-x <width>` | Window width | 1000 |
| `-y <height>` | Window height | 1000 |
| `-t, --threads <n>` | Number of threads | 12 |
| `-h, --help` | Show help | - |

### Examples

```bash
# Basic usage
./display_image -f /path/to/simulation/output

# Large window, 8 threads
./display_image -f ./images -x 1920 -y 1080 -t 8

# Quick preview (every 10th image)
./display_image -f ./images -n 10

# Force shrink factor
./display_image -f ./images -s 4
```

## Controls

| Key/Action | Function |
|------------|----------|
| Left/Right Arrow, A/D | Previous/Next image |
| Home | First image |
| End | Last image |
| Space | Play/Pause |
| J | Reverse playback direction |
| Mouse Wheel | Zoom in/out |
| Left Mouse Drag | Pan |
| R | Reset zoom/pan |
| Q / Escape | Quit |

## File Naming Convention

Images must match the pattern: `*_<number>.png`

Examples:
- `frame_000001.png`
- `image_123.png`
- `e_png_yx_0.5_015000.png`

Images are automatically sorted by their numeric suffix.

## Differences from Windows Version

| Feature | Windows | Linux |
|---------|---------|-------|
| Graphics API | GDI | SDL2 |
| Folder dialog | Built-in | Command-line `-f` |
| File dialog | Built-in | Not yet implemented |
| MP4 Export | Yes | Not yet implemented |
| Color format | BGR (bottom-up) | RGB (top-down) |

## Future Enhancements

- [ ] MP4 export via FFmpeg
- [ ] GUI folder selection (using zenity or tinyfiledialogs)
- [ ] File save dialog for exports
- [ ] Keyboard shortcut overlay

## Troubleshooting

### "SDL initialization failed"
Make sure SDL2 is properly installed and X11/Wayland is running.

### "Could not open directory"
Check that the folder path exists and you have read permissions.

### Black window
Verify that images exist and match the `*_<number>.png` pattern.
