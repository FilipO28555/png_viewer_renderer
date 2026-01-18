# Porting Guide: Windows → Linux

## Overview

The core logic (image loading, zoom/pan math, multi-threaded export, FFmpeg piping) is **platform-independent** and can stay mostly unchanged. You only need to replace the **Windows-specific UI and file handling** code.

## What to Replace

| Component | Windows Code | Linux Replacement |
|-----------|-------------|-------------------|
| **Window & Graphics** | `CreateWindow`, `WNDCLASS`, `StretchDIBits`, GDI | **SDL2** (recommended) or X11 |
| **Event Loop** | `GetMessage`, `PeekMessage`, `WM_*` messages | SDL2 event loop |
| **File Dialogs** | `SHBrowseForFolder`, `GetSaveFileName` | Command-line args, or `zenity`/`tinyfiledialogs` |
| **File Search** | `FindFirstFile`, `WIN32_FIND_DATA` | `opendir`/`readdir` or `<dirent.h>` |
| **Path Separator** | `\\` | `/` |
| **Pipe to FFmpeg** | `_popen`, `_pclose` | `popen`, `pclose` (same API, different name) |
| **Timer** | `QueryPerformanceCounter` | `clock_gettime` or `std::chrono` |

## What Stays the Same ✓

- `stb_image.h` - works everywhere
- All zoom/pan/rendering math
- Multi-threaded loading (`std::thread`, `std::mutex`, etc.)
- Multi-threaded export with bounded queue
- Frame sorting and file pattern matching logic
- FFmpeg command construction

## Recommended Approach: SDL2

SDL2 is cross-platform and much simpler than raw X11.

### Window and Renderer Setup

```cpp
#include <SDL2/SDL.h>

// Initialize SDL
SDL_Init(SDL_INIT_VIDEO);

// Create window
SDL_Window* window = SDL_CreateWindow(
    "PNG Viewer",
    SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
    WINDOW_WIDTH, WINDOW_HEIGHT,
    0
);

// Create renderer
SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

// Create texture for the current frame
SDL_Texture* texture = SDL_CreateTexture(
    renderer,
    SDL_PIXELFORMAT_RGB24,
    SDL_TEXTUREACCESS_STREAMING,
    imageWidth, imageHeight
);
```

### Rendering a Frame

```cpp
void RenderFrame(int frameIdx) {
    if (frameIdx < 0 || frameIdx >= frames.size()) return;
    
    // Update texture with image data
    // Note: SDL expects RGB, our data is BGR (from Windows version)
    // You may need to swap R and B channels, or change the load function
    SDL_UpdateTexture(texture, NULL, frames[frameIdx].data, imageWidth * 3);
    
    // Calculate source and destination rectangles for zoom/pan
    SDL_Rect srcRect = { /* based on zoom/pan */ };
    SDL_Rect dstRect = { 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };
    
    // Clear and render
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, &srcRect, &dstRect);
    SDL_RenderPresent(renderer);
}
```

### Event Loop

```cpp
bool running = true;
SDL_Event event;

while (running) {
    // Process events
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                running = false;
                break;
                
            case SDL_KEYDOWN:
                switch (event.key.keysym.sym) {
                    case SDLK_q:
                        running = false;
                        break;
                    case SDLK_LEFT:
                    case SDLK_a:
                        if (currentFrame > 0) currentFrame--;
                        break;
                    case SDLK_RIGHT:
                    case SDLK_d:
                        if (currentFrame < frames.size() - 1) currentFrame++;
                        break;
                    case SDLK_SPACE:
                        isPlaying = !isPlaying;
                        break;
                    case SDLK_r:
                        ResetView();
                        break;
                    case SDLK_s:
                        ExportToMP4();
                        break;
                    case SDLK_HOME:
                        currentFrame = 0;
                        break;
                    case SDLK_END:
                        currentFrame = frames.size() - 1;
                        break;
                    case SDLK_j:
                        playDirection = -playDirection;
                        break;
                }
                break;
                
            case SDL_MOUSEWHEEL:
                // Zoom
                {
                    int mouseX, mouseY;
                    SDL_GetMouseState(&mouseX, &mouseY);
                    double zoomFactor = (event.wheel.y > 0) ? 1.15 : (1.0 / 1.15);
                    // Apply zoom logic (same as Windows version)
                }
                break;
                
            case SDL_MOUSEBUTTONDOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    isDragging = true;
                    lastMouseX = event.button.x;
                    lastMouseY = event.button.y;
                }
                break;
                
            case SDL_MOUSEBUTTONUP:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    isDragging = false;
                }
                break;
                
            case SDL_MOUSEMOTION:
                if (isDragging) {
                    // Pan logic (same as Windows version)
                    int dx = event.motion.x - lastMouseX;
                    int dy = event.motion.y - lastMouseY;
                    // Apply pan...
                    lastMouseX = event.motion.x;
                    lastMouseY = event.motion.y;
                }
                break;
        }
    }
    
    // Update playback
    if (isPlaying) {
        currentFrame += playDirection;
        if (currentFrame >= frames.size()) currentFrame = 0;
        if (currentFrame < 0) currentFrame = frames.size() - 1;
    }
    
    // Render
    RenderFrame(currentFrame);
    
    // Frame rate limiting (if not playing, wait for events)
    if (!isPlaying) {
        SDL_WaitEvent(NULL);
    }
}

// Cleanup
SDL_DestroyTexture(texture);
SDL_DestroyRenderer(renderer);
SDL_DestroyWindow(window);
SDL_Quit();
```

## File Search Replacement

Replace `FindFirstFile`/`FindNextFile` with POSIX `opendir`/`readdir`:

```cpp
#include <dirent.h>
#include <sys/stat.h>
#include <cstring>

std::vector<std::string> FindMatchingFiles(const std::string& pattern) {
    std::vector<std::string> files;
    
    // Get directory from pattern (or use current directory)
    std::string directory = ".";
    
    DIR* dir = opendir(directory.c_str());
    if (!dir) {
        std::cerr << "Could not open directory: " << directory << std::endl;
        return files;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        std::string name = entry->d_name;
        
        // Skip . and ..
        if (name == "." || name == "..") continue;
        
        // Check if it's a .png file
        if (name.size() > 4 && name.substr(name.size() - 4) == ".png") {
            // Check if it's a regular file
            struct stat st;
            if (stat(name.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
                files.push_back(name);
            }
        }
    }
    
    closedir(dir);
    return files;
}
```

## Timer Replacement

Replace `QueryPerformanceCounter` with `std::chrono`:

```cpp
#include <chrono>

// Instead of LARGE_INTEGER and QueryPerformanceCounter:
using Clock = std::chrono::high_resolution_clock;
using TimePoint = std::chrono::time_point<Clock>;

TimePoint startTime = Clock::now();

// Later, to get elapsed time in seconds:
TimePoint currentTime = Clock::now();
double elapsed = std::chrono::duration<double>(currentTime - startTime).count();
```

## FFmpeg Pipe

Just rename `_popen` to `popen` and `_pclose` to `pclose`:

```cpp
// Windows:
FILE* ffmpeg = _popen(cmd, "wb");
_pclose(ffmpeg);

// Linux:
FILE* ffmpeg = popen(cmd, "w");  // Note: "w" not "wb" on Linux
pclose(ffmpeg);
```

## Path Handling

```cpp
#ifdef _WIN32
    const char PATH_SEP = '\\';
#else
    const char PATH_SEP = '/';
#endif

// Or just use '/' everywhere - it works on Windows too in most cases
std::string fullPath = folder + "/" + filename;
```

## Command-Line Folder Selection

Instead of `SHBrowseForFolder`, accept the folder as a command-line argument:

```cpp
int main(int argc, char* argv[]) {
    std::string folder = ".";  // Default to current directory
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--folder") == 0) {
            if (i + 1 < argc) {
                folder = argv[i + 1];
                i++;
            }
        }
        // ... other arguments
    }
    
    if (!LoadImagesFromFolder(folder)) {
        std::cerr << "Failed to load images from: " << folder << std::endl;
        return -1;
    }
    
    // ...
}
```

Usage:
```bash
./display_image -f /path/to/images -x 1920 -y 1080
```

## Compilation on Linux

### Install Dependencies

```bash
# Debian/Ubuntu
sudo apt install libsdl2-dev ffmpeg g++

# Fedora
sudo dnf install SDL2-devel ffmpeg gcc-c++

# Arch
sudo pacman -S sdl2 ffmpeg gcc
```

### Compile

```bash
g++ -o display_image display_image_linux.cpp \
    $(sdl2-config --cflags --libs) \
    -lpthread -O2 -std=c++11
```

Or with a simple Makefile:

```makefile
CXX = g++
CXXFLAGS = -O2 -std=c++11 $(shell sdl2-config --cflags)
LDFLAGS = $(shell sdl2-config --libs) -lpthread

display_image: display_image_linux.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f display_image
```

## Estimated Effort

| Task | Time |
|------|------|
| Set up SDL2 window and renderer | 1 hour |
| Port rendering with zoom/pan | 2-3 hours |
| Port keyboard/mouse event handling | 1-2 hours |
| Replace file search with opendir | 30 min |
| Replace timers with std::chrono | 15 min |
| Add command-line folder argument | 15 min |
| Testing & debugging | 1-2 hours |
| **Total** | **6-9 hours** |

## Tips

1. **Start simple**: Get a window displaying a static image first
2. **Fix color order**: Windows GDI uses BGR, SDL2 uses RGB - you may need to adjust `LoadAndShrinkImage()`
3. **Test export early**: The FFmpeg export should work with minimal changes
4. **Use printf debugging**: SDL2 doesn't have a console by default, use `fprintf(stderr, ...)` or redirect output
5. **Consider a build system**: CMake can help manage cross-platform builds

## Optional: Cross-Platform Build

For a true cross-platform codebase, consider:

1. **Use `#ifdef` for platform-specific code**:
   ```cpp
   #ifdef _WIN32
       // Windows code
   #else
       // Linux/Unix code
   #endif
   ```

2. **Use CMake** for build configuration

3. **Use SDL2 on both platforms** - it works on Windows too!
