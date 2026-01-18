// Linux SDL2-based PNG viewer using stb_image
// A port of the Windows GDI version for Linux systems

// For compiling common code with stb_image implementation
#define STB_IMAGE_IMPLEMENTATION
#include "../common/stb_image.h"
#include "../common/frame_types.h"
#include "../common/math_utils.h"
#include "../common/image_loader.h"

#include <SDL2/SDL.h>
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <dirent.h>
#include <sys/stat.h>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <iomanip>
#include <limits>
#include <map>
#include <condition_variable>
#include <atomic>
#include <thread>

// Multi-threaded MP4 export using ffmpeg (forward declaration)
void ExportToMP4_MT();

// Helper: render current view into RGB24 buffer using full-resolution image
static void RenderViewToBufferHQ(
    unsigned char* buffer,
    int outW,
    int outH,
    const unsigned char* src,
    int srcW,
    int srcH,
    const ViewState& view,
    const AppSettings& settings
) {
    // Compute fit scale as in GetFitScale
    double scaleX = static_cast<double>(settings.windowWidth) / srcW;
    double scaleY = static_cast<double>(settings.windowHeight) / srcH;
    double fitScale = std::min(scaleX, scaleY);

    double currentScale = fitScale * view.zoomLevel;

    // Visible region in source coordinates (for reference)
    double visibleW = settings.windowWidth / currentScale;
    double visibleH = settings.windowHeight / currentScale;

    // Center of view in source coordinates (full-res)
    // Interpret panX/panY as offsets in window space, scaled into source space
    double centerX = srcW / 2.0 + view.panX * (visibleW / settings.windowWidth);
    double centerY = srcH / 2.0 + view.panY * (visibleH / settings.windowHeight);

    // For each output pixel, map back to source image
    for (int y = 0; y < outH; ++y) {
        for (int x = 0; x < outW; ++x) {
            double srcX = (x - outW / 2.0) / currentScale + centerX;
            double srcY = (y - outH / 2.0) / currentScale + centerY;

            int ix = static_cast<int>(srcX);
            int iy = static_cast<int>(srcY);

            unsigned char* dst = buffer + (static_cast<size_t>(y) * outW + x) * 3;

            if (ix >= 0 && ix < srcW && iy >= 0 && iy < srcH) {
                const unsigned char* srcPix = src + (static_cast<size_t>(iy) * srcW + ix) * 3;
                dst[0] = srcPix[0];
                dst[1] = srcPix[1];
                dst[2] = srcPix[2];
            } else {
                dst[0] = dst[1] = dst[2] = 0;
            }
        }
    }
}

// Signal handler for Ctrl+C
void signalHandler(int signum) {
    g_interrupted.store(true);
}

// Global state
AppSettings g_settings;
ViewState g_view;
ImageCollection g_images;

// SDL resources
SDL_Window* g_window = nullptr;
SDL_Renderer* g_renderer = nullptr;
SDL_Texture* g_texture = nullptr;

// Find all PNG files in a directory
std::vector<std::string> FindPngFiles(const std::string& directory) {
    std::vector<std::string> files;
    
    DIR* dir = opendir(directory.c_str());
    if (!dir) {
        std::cerr << "Could not open directory: " << directory << std::endl;
        return files;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        
        // Skip . and ..
        if (name == "." || name == "..") continue;
        
        // Check if it's a .png file
        if (name.size() > 4 && name.substr(name.size() - 4) == ".png") {
            std::string fullPath = directory + "/" + name;
            struct stat st;
            if (stat(fullPath.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
                files.push_back(name);
            }
        }
    }
    
    closedir(dir);
    return files;
}

// Load images from folder
bool LoadImagesFromFolder(const std::string& folder) {
    int shrinkFactor = g_settings.shrinkFactor;
    
    // Find all PNG files
    std::vector<std::string> allFiles = FindPngFiles(folder);
    
    // Filter and sort by index
    std::vector<std::pair<std::string, int>> validFiles;
    for (const auto& file : allFiles) {
        int idx = ExtractIndex(file);
        if (idx >= 0) {
            validFiles.push_back({file, idx});
        }
    }
    
    // Sort by numeric index
    std::sort(validFiles.begin(), validFiles.end(),
        [](const auto& a, const auto& b) { return a.second < b.second; });
    
    if (validFiles.empty()) {
        std::cerr << "No matching files found in " << folder << std::endl;
        return false;
    }
    
    // Auto-calculate shrink factor if needed
    if (shrinkFactor == 0) {
        std::string probeFile = folder + "/" + validFiles[0].first;
        shrinkFactor = AutoCalculateShrinkFactor(probeFile, g_settings.windowWidth, g_settings.windowHeight);
    }
    
    // Select every n-th file
    std::vector<std::string> files;
    for (size_t i = 0; i < validFiles.size(); i += g_settings.nthFrame) {
        files.push_back(folder + "/" + validFiles[i].first);
    }
    // Always include last frame
    if (!validFiles.empty() && (validFiles.size() - 1) % g_settings.nthFrame != 0) {
        files.push_back(folder + "/" + validFiles.back().first);
    }
    
    // All file paths for export
    std::vector<std::string> allFilePaths;
    for (const auto& vf : validFiles) {
        allFilePaths.push_back(folder + "/" + vf.first);
    }
    
    std::cout << "Found " << validFiles.size() << " matching images (*_<number>.png)" << std::endl;
    if (g_settings.nthFrame > 1) {
        std::cout << "Loading every " << g_settings.nthFrame << "-th image: " << files.size() << " images" << std::endl;
    }
    
    // Load images (RGB output, no vertical flip for SDL2)
    bool success = LoadImagesCommon(
        g_images, files, allFilePaths, folder,
        shrinkFactor, g_settings.numThreads,
        true,   // rgbOutput
        false   // flipVertical (SDL2 is top-down like stb_image)
    );
    
    if (success) {
        g_view.reset();
    }
    
    return success;
}

// Create or recreate the texture for current image dimensions
bool CreateTexture() {
    if (g_texture) {
        SDL_DestroyTexture(g_texture);
        g_texture = nullptr;
    }
    
    if (g_images.imageWidth == 0 || g_images.imageHeight == 0) {
        return false;
    }
    
    g_texture = SDL_CreateTexture(
        g_renderer,
        SDL_PIXELFORMAT_RGB24,
        SDL_TEXTUREACCESS_STREAMING,
        g_images.imageWidth, g_images.imageHeight
    );
    
    if (!g_texture) {
        std::cerr << "Failed to create texture: " << SDL_GetError() << std::endl;
        return false;
    }
    
    return true;
}

// Update window title
void UpdateWindowTitle() {
    if (!g_window || g_images.isEmpty()) return;
    
    char title[256];
    if (g_view.isPlaying) {
        const char* direction = (g_view.playDirection > 0) ? ">" : "<";
        snprintf(title, sizeof(title), "%s [%d/%zu] - %.1f FPS %s",
                 g_images.frames[g_images.currentFrame].filename.c_str(),
                 g_images.currentFrame + 1,
                 g_images.size(),
                 g_view.currentFPS,
                 direction);
    } else {
        snprintf(title, sizeof(title), "%s [%d/%zu] - Zoom: %.0f%%",
                 g_images.frames[g_images.currentFrame].filename.c_str(),
                 g_images.currentFrame + 1,
                 g_images.size(),
                 g_view.zoomLevel * 100.0);
    }
    SDL_SetWindowTitle(g_window, title);
}

// Render current frame
void RenderFrame() {
    // Clear to black
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_renderer);
    
    if (g_images.isEmpty() || !g_texture) {
        SDL_RenderPresent(g_renderer);
        return;
    }
    
    // Update texture with current frame data
    unsigned char* frameData = g_images.frames[g_images.currentFrame].data;
    if (!frameData) {
        SDL_RenderPresent(g_renderer);
        return;
    }
    
    SDL_UpdateTexture(g_texture, nullptr, frameData, g_images.imageWidth * 3);
    
    // Calculate render parameters
    RenderParams params = CalculateRenderParams(g_view, g_settings, 
                                                 g_images.imageWidth, g_images.imageHeight);
    
    // Source and destination rectangles
    SDL_Rect srcRect = {params.srcX, params.srcY, params.srcW, params.srcH};
    SDL_Rect dstRect = {params.dstX, params.dstY, params.dstW, params.dstH};
    
    // Render with linear filtering for smooth zoom
    SDL_SetTextureScaleMode(g_texture, SDL_ScaleModeLinear);
    SDL_RenderCopy(g_renderer, g_texture, &srcRect, &dstRect);
    
    SDL_RenderPresent(g_renderer);
}

// Parse command line arguments
void ParseArguments(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--shrink") == 0) && i + 1 < argc) {
            g_settings.shrinkFactor = std::max(1, atoi(argv[i + 1]));
            i++;
        }
        else if ((strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--nth") == 0) && i + 1 < argc) {
            g_settings.nthFrame = std::max(1, atoi(argv[i + 1]));
            i++;
        }
        else if (strcmp(argv[i], "-x") == 0 && i + 1 < argc) {
            g_settings.windowWidth = std::clamp(atoi(argv[i + 1]), 100, 7680);
            i++;
        }
        else if (strcmp(argv[i], "-y") == 0 && i + 1 < argc) {
            g_settings.windowHeight = std::clamp(atoi(argv[i + 1]), 100, 4320);
            i++;
        }
        else if ((strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--threads") == 0) && i + 1 < argc) {
            g_settings.numThreads = std::clamp(atoi(argv[i + 1]), 1, 64);
            i++;
        }
        else if ((strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--folder") == 0) && i + 1 < argc) {
            g_settings.initialFolder = argv[i + 1];
            i++;
        }
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  -f, --folder <path>    Folder containing images (required)" << std::endl;
            std::cout << "  -s, --shrink <factor>  Shrink factor for images (default: auto)" << std::endl;
            std::cout << "  -n, --nth <n>          Load every n-th image (default: 1)" << std::endl;
            std::cout << "  -x <width>             Window width in pixels (default: 1000)" << std::endl;
            std::cout << "  -y <height>            Window height in pixels (default: 1000)" << std::endl;
            std::cout << "  -t, --threads <n>      Number of threads (default: 12)" << std::endl;
            std::cout << "  -h, --help             Show this help message" << std::endl;
            std::cout << "\nControls:" << std::endl;
            std::cout << "  Left/Right Arrow, A/D: Navigate frames" << std::endl;
            std::cout << "  Home/End: First/Last frame" << std::endl;
            std::cout << "  Space: Play/Pause" << std::endl;
            std::cout << "  J: Reverse playback direction" << std::endl;
            std::cout << "  Mouse Wheel: Zoom in/out" << std::endl;
            std::cout << "  Left Drag: Pan" << std::endl;
            std::cout << "  R: Reset view" << std::endl;
            std::cout << "  Q/Escape: Quit" << std::endl;
            exit(0);
        }
    }
}

int main(int argc, char* argv[]) {
    // Install signal handler for Ctrl+C
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // Parse command line
    ParseArguments(argc, argv);
    
    std::cout << "PNG Image Viewer (Linux/SDL2)" << std::endl;
    std::cout << "=============================" << std::endl;
    std::cout << "Window: " << g_settings.windowWidth << " x " << g_settings.windowHeight << std::endl;
    std::cout << "Shrink factor: " << (g_settings.shrinkFactor == 0 ? "auto" : std::to_string(g_settings.shrinkFactor)) << std::endl;
    std::cout << "Load every " << g_settings.nthFrame << "-th image" << std::endl;
    std::cout << "Threads: " << g_settings.numThreads << std::endl;
    
    // Check for folder argument
    if (g_settings.initialFolder.empty()) {
        std::cerr << "\nError: No folder specified!" << std::endl;
        std::cerr << "Usage: " << argv[0] << " -f <folder_path>" << std::endl;
        std::cerr << "Run with -h for help." << std::endl;
        return -1;
    }
    
    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL initialization failed: " << SDL_GetError() << std::endl;
        return -1;
    }
    
    // Create window
    g_window = SDL_CreateWindow(
        "PNG Image Viewer",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        g_settings.windowWidth, g_settings.windowHeight,
        SDL_WINDOW_SHOWN
    );
    
    if (!g_window) {
        std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return -1;
    }
    
    // Create renderer
    g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_renderer) {
        std::cerr << "Renderer creation failed: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(g_window);
        SDL_Quit();
        return -1;
    }
    
    // Load images
    if (!LoadImagesFromFolder(g_settings.initialFolder)) {
        std::cerr << "Failed to load images from: " << g_settings.initialFolder << std::endl;
        SDL_DestroyRenderer(g_renderer);
        SDL_DestroyWindow(g_window);
        SDL_Quit();
        return -1;
    }
    
    // Create texture
    if (!CreateTexture()) {
        g_images.cleanup();
        SDL_DestroyRenderer(g_renderer);
        SDL_DestroyWindow(g_window);
        SDL_Quit();
        return -1;
    }
    
    UpdateWindowTitle();
    
    // Timing for FPS calculation
    using Clock = std::chrono::high_resolution_clock;
    auto lastFrameTime = Clock::now();
    
    // Main loop
    bool running = true;
    SDL_Event event;
    
    while (running && !g_interrupted.load()) {
        // Process events
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                
                case SDL_KEYDOWN:
                    switch (event.key.keysym.sym) {
                        case SDLK_q:
                        case SDLK_ESCAPE:
                            running = false;
                            break;
                        
                        case SDLK_LEFT:
                        case SDLK_a:
                            if (g_images.currentFrame > 0) {
                                g_images.currentFrame--;
                                UpdateWindowTitle();
                            }
                            break;
                        
                        case SDLK_RIGHT:
                        case SDLK_d:
                            if (g_images.currentFrame < (int)g_images.size() - 1) {
                                g_images.currentFrame++;
                                UpdateWindowTitle();
                            }
                            break;
                        
                        case SDLK_HOME:
                            g_images.currentFrame = 0;
                            UpdateWindowTitle();
                            break;
                        
                        case SDLK_END:
                            g_images.currentFrame = (int)g_images.size() - 1;
                            UpdateWindowTitle();
                            break;
                        
                        case SDLK_SPACE:
                            g_view.isPlaying = !g_view.isPlaying;
                            if (g_view.isPlaying) {
                                lastFrameTime = Clock::now();
                                g_view.frameCount = 0;
                                g_view.fpsAccumulator = 0.0;
                            }
                            UpdateWindowTitle();
                            break;
                        
                        case SDLK_j:
                            g_view.playDirection = -g_view.playDirection;
                            UpdateWindowTitle();
                            break;
                        
                        case SDLK_r:
                            g_view.reset();
                            UpdateWindowTitle();
                            break;
                        case SDLK_s:
                            g_view.isPlaying = false;
                            std::cout << "\n[S] pressed: starting MULTI-THREADED MP4 export..." << std::endl;
                            ExportToMP4_MT();
                            break;
                    }
                    break;
                
                case SDL_MOUSEWHEEL: {
                    int mouseX, mouseY;
                    SDL_GetMouseState(&mouseX, &mouseY);
                    double zoomFactor = (event.wheel.y > 0) ? 1.15 : (1.0 / 1.15);
                    ApplyZoom(g_view, g_settings, g_images.imageWidth, g_images.imageHeight,
                              mouseX, mouseY, zoomFactor);
                    UpdateWindowTitle();
                    break;
                }
                
                case SDL_MOUSEBUTTONDOWN:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        g_view.isDragging = true;
                        g_view.lastMouseX = event.button.x;
                        g_view.lastMouseY = event.button.y;
                    }
                    break;
                
                case SDL_MOUSEBUTTONUP:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        g_view.isDragging = false;
                    }
                    break;
                
                case SDL_MOUSEMOTION:
                    if (g_view.isDragging) {
                        ApplyPan(g_view, g_settings, g_images.imageWidth, g_images.imageHeight,
                                 event.motion.x, event.motion.y);
                    }
                    break;
            }
        }
        
        // Update playback
        if (g_view.isPlaying) {
            int nextFrame = g_images.currentFrame + g_view.playDirection;
            
            // Wrap around
            if (nextFrame >= (int)g_images.size()) {
                nextFrame = 0;
            } else if (nextFrame < 0) {
                nextFrame = (int)g_images.size() - 1;
            }
            
            g_images.currentFrame = nextFrame;
            
            // Calculate FPS
            auto currentTime = Clock::now();
            double deltaTime = std::chrono::duration<double>(currentTime - lastFrameTime).count();
            lastFrameTime = currentTime;
            
            g_view.frameCount++;
            g_view.fpsAccumulator += deltaTime;
            
            if (g_view.frameCount >= 10) {
                g_view.currentFPS = g_view.frameCount / g_view.fpsAccumulator;
                g_view.frameCount = 0;
                g_view.fpsAccumulator = 0.0;
            }
            
            UpdateWindowTitle();
        }
        
        // Render
        RenderFrame();
        
        // If not playing, wait for events to save CPU
        if (!g_view.isPlaying) {
            SDL_WaitEvent(nullptr);
        }
    }
    
    // Cleanup
    g_images.cleanup();
    if (g_texture) SDL_DestroyTexture(g_texture);
    SDL_DestroyRenderer(g_renderer);
    SDL_DestroyWindow(g_window);
    SDL_Quit();
    
    return 0;
}

// Multi-threaded MP4 export using ffmpeg
void ExportToMP4_MT() {
    if (g_images.allFilePaths.empty()) {
        std::cerr << "No images loaded to export!" << std::endl;
        return;
    }

    bool wasPlaying = g_view.isPlaying;
    g_view.isPlaying = false;

    int fps = 30;
    int winW = g_settings.windowWidth;
    int winH = g_settings.windowHeight;
    size_t totalFrames = g_images.allFilePaths.size();
    int numExportThreads = g_settings.numThreads;
    if (numExportThreads < 1) numExportThreads = 1;

    size_t frameBufferSize = static_cast<size_t>(winW) * winH * 3;

    std::string filename = "export_output_mt.mp4";
    std::cout << "\n[S] pressed: starting MULTI-THREADED MP4 export..." << std::endl;
    std::cout << "Output file : " << filename << std::endl;
    std::cout << "Resolution  : " << winW << " x " << winH << std::endl;
    std::cout << "FPS         : " << fps << std::endl;
    std::cout << "Total frames: " << totalFrames << std::endl;
    std::cout << "Threads     : " << numExportThreads << std::endl;

    char cmd[1024];
    std::snprintf(cmd, sizeof(cmd),
        "ffmpeg -y -f rawvideo -pixel_format rgb24 -video_size %dx%d -framerate %d -i - "
        "-c:v libx264 -pix_fmt yuv420p -crf 18 \"%s\"",
        winW, winH, fps, filename.c_str());

    FILE* ffmpeg = popen(cmd, "w");
    if (!ffmpeg) {
        std::cerr << "Failed to start ffmpeg. Is it installed and in PATH?" << std::endl;
        g_view.isPlaying = wasPlaying;
        return;
    }

    std::mutex queueMutex;
    std::condition_variable queueNotFull;
    std::condition_variable queueNotEmpty;
    std::map<size_t, unsigned char*> renderedFrames;
    std::atomic<size_t> nextFrameToRender(0);
    size_t nextFrameToWrite = 0;

    size_t maxQueueSize = static_cast<size_t>(numExportThreads) * 2;

    ViewState capturedView = g_view;
    AppSettings capturedSettings = g_settings;

    auto renderWorker = [&]() {
        while (true) {
            size_t idx = nextFrameToRender.fetch_add(1);
            if (idx >= totalFrames) break;
            if (g_interrupted.load()) break;

            unsigned char* buffer = new unsigned char[frameBufferSize];
            std::memset(buffer, 0, frameBufferSize);

            int w, h, channels;
            unsigned char* data = stbi_load(g_images.allFilePaths[idx].c_str(), &w, &h, &channels, 3);
            if (data) {
                RenderViewToBufferHQ(buffer, winW, winH,
                                     data, w, h,
                                     capturedView, capturedSettings);
                stbi_image_free(data);
            }

            {
                std::unique_lock<std::mutex> lock(queueMutex);
                queueNotFull.wait(lock, [&]() {
                    return renderedFrames.size() < maxQueueSize || g_interrupted.load();
                });
                if (g_interrupted.load()) {
                    delete[] buffer;
                    break;
                }
                renderedFrames[idx] = buffer;
            }
            queueNotEmpty.notify_one();
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(numExportThreads);
    for (int i = 0; i < numExportThreads; ++i) {
        workers.emplace_back(renderWorker);
    }

    auto start = std::chrono::high_resolution_clock::now();

    while (nextFrameToWrite < totalFrames && !g_interrupted.load()) {
        unsigned char* frameData = nullptr;

        {
            std::unique_lock<std::mutex> lock(queueMutex);
            queueNotEmpty.wait(lock, [&]() {
                return renderedFrames.find(nextFrameToWrite) != renderedFrames.end()
                       || g_interrupted.load();
            });

            if (g_interrupted.load()) break;

            frameData = renderedFrames[nextFrameToWrite];
            renderedFrames.erase(nextFrameToWrite);
        }
        queueNotFull.notify_one();

        std::fwrite(frameData, 1, frameBufferSize, ffmpeg);
        delete[] frameData;

        ++nextFrameToWrite;

        double progress = 100.0 * nextFrameToWrite / totalFrames;
        auto now = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double>(now - start).count();
        double fps_actual = nextFrameToWrite / std::max(0.001, elapsed);
        double eta = (totalFrames - nextFrameToWrite) / std::max(0.001, fps_actual);

        if (g_window) {
            char title[256];
            std::snprintf(title, sizeof(title),
                          "Exporting MT: %zu/%zu (%.1f%%)",
                          nextFrameToWrite, totalFrames, progress);
            SDL_SetWindowTitle(g_window, title);
        }

        std::cout << "\rFrame " << nextFrameToWrite << "/" << totalFrames
                  << " (" << std::fixed << std::setprecision(1) << progress << "%)"
                  << " - " << fps_actual << " fps"
                  << " - ETA: " << (int)(eta / 60) << "m " << (int)eta % 60 << "s" << std::flush;
    }

    for (auto& t : workers) {
        if (t.joinable()) t.join();
    }

    std::cout << std::endl;
    pclose(ffmpeg);

    auto end = std::chrono::high_resolution_clock::now();
    double totalTime = std::chrono::duration<double>(end - start).count();

    if (g_interrupted.load()) {
        std::cout << "\nExport interrupted by user." << std::endl;
    } else {
        std::cout << "\nExport complete in " << (int)(totalTime / 60) << "m "
                  << (int)totalTime % 60 << "s" << std::endl;
    }

    UpdateWindowTitle();
    g_view.isPlaying = wasPlaying;
}
