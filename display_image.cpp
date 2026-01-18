// Windows GDI-based PNG viewer using stb_image
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>
#include <commdlg.h>
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <map>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <iomanip>

// Structure to hold image data
struct ImageFrame {
    std::string filename;
    int index;  // The numeric part (e.g., 000100 -> 100)
    unsigned char* data;
};

// Constants
int WINDOW_WIDTH = 1000;
int WINDOW_HEIGHT = 1000;

int shrinkFactor_global = 0;  // 0 = auto-calculate based on window size
int nthFrame_global = 1;      // Load every n-th frame (1 = all frames)
int numThreads_global = 12;   // Number of threads for loading and export

// Global variables
std::vector<ImageFrame> frames;
std::vector<std::string> allFilePaths;  // All files in folder for full-quality export
int currentFrame = 0;
int imageWidth = 0;
int imageHeight = 0;
int originalImageWidth = 0;   // Original (non-shrunk) dimensions
int originalImageHeight = 0;
BITMAPINFO* bmi = nullptr;
HWND g_hwnd = nullptr;

// Zoom and pan variables
double zoomLevel = 1.0;       // 1.0 = fit to window
double minZoom = 1.0;         // Will be calculated to fit image in window
double maxZoom = 10.0;        // Maximum zoom level
double panX = 0.0;            // Pan offset in image coordinates
double panY = 0.0;
bool isDragging = false;
int lastMouseX = 0;
int lastMouseY = 0;

// Playback variables
bool isPlaying = false;
int playDirection = 1;        // 1 = forward, -1 = backward
double currentFPS = 0.0;
LARGE_INTEGER perfFrequency;
LARGE_INTEGER lastFrameTime;
int frameCount = 0;
double fpsAccumulator = 0.0;

// Folder selection
std::string currentFolder;
bool needsReload = false;
bool exportRequested = false;

// Function declarations
bool LoadImagesFromFolder(const std::string& folder);
std::string SelectFolder(const std::string& initialFolder = "");
void CleanupFrames();
void ExportToMP4();
double GetFitScale();
void UpdateWindowTitle();

// Render current view to a buffer (for export) - uses original full-res files
void RenderViewToBufferHQ(unsigned char* buffer, const std::string& filepath) {
    // Clear buffer to black
    memset(buffer, 0, WINDOW_WIDTH * WINDOW_HEIGHT * 3);
    
    // Load the original full-resolution image
    int w, h, channels;
    unsigned char* originalData = stbi_load(filepath.c_str(), &w, &h, &channels, 3);
    if (!originalData) {
        std::cerr << "Failed to load: " << filepath << std::endl;
        return;
    }
    
    // Calculate scale based on ORIGINAL image dimensions (not shrunk)
    double scaleX = (double)WINDOW_WIDTH / originalImageWidth;
    double scaleY = (double)WINDOW_HEIGHT / originalImageHeight;
    double fitScale = (scaleX < scaleY) ? scaleX : scaleY;
    double currentScale = fitScale * zoomLevel;
    
    // Pan is in shrunk image coordinates, convert to original coordinates
    double panXOriginal = panX * shrinkFactor_global;
    double panYOriginal = panY * shrinkFactor_global;
    
    double centerX = w / 2.0 + panXOriginal;
    double centerY = h / 2.0 + panYOriginal;
    
    // For each pixel in output buffer
    for (int outY = 0; outY < WINDOW_HEIGHT; outY++) {
        for (int outX = 0; outX < WINDOW_WIDTH; outX++) {
            // Map output pixel to original image coordinates
            double imgX = (outX - WINDOW_WIDTH / 2.0) / currentScale + centerX;
            double imgY = (outY - WINDOW_HEIGHT / 2.0) / currentScale + centerY;
            
            int srcX = (int)imgX;
            int srcY = (int)imgY;
            
            if (srcX >= 0 && srcX < w && srcY >= 0 && srcY < h) {
                // stb_image loads top-down RGB
                int srcIdx = (srcY * w + srcX) * 3;
                int dstIdx = (outY * WINDOW_WIDTH + outX) * 3;
                // Copy RGB directly (FFmpeg expects RGB)
                buffer[dstIdx + 0] = originalData[srcIdx + 0];  // R
                buffer[dstIdx + 1] = originalData[srcIdx + 1];  // G
                buffer[dstIdx + 2] = originalData[srcIdx + 2];  // B
            }
        }
    }
    
    stbi_image_free(originalData);
}

// Export current view as MP4 using FFmpeg - HIGH QUALITY using original files
// Multi-threaded: worker threads render frames, main thread writes sequentially
void ExportToMP4() {
    if (allFilePaths.empty()) {
        MessageBoxA(g_hwnd, "No images loaded to export!", "Export Error", MB_OK | MB_ICONERROR);
        return;
    }
    
    // Ask for output filename
    char filename[MAX_PATH] = "output.mp4";
    OPENFILENAMEA ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = "MP4 Files\0*.mp4\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = "Save MP4 As";
    ofn.Flags = OFN_OVERWRITEPROMPT;
    ofn.lpstrDefExt = "mp4";
    
    if (!GetSaveFileNameA(&ofn)) {
        return;  // User cancelled
    }
    
    // Configuration
    int fps = 30;
    int numExportThreads = numThreads_global;
    size_t frameBufferSize = (size_t)WINDOW_WIDTH * WINDOW_HEIGHT * 3;
    size_t originalImageSize = (size_t)originalImageWidth * originalImageHeight * 3;
    
    // Limit queue size to prevent excessive memory usage
    size_t maxQueueSize = numExportThreads * 2;
    
    std::cout << "\n=== HIGH QUALITY MULTI-THREADED EXPORT ===" << std::endl;
    std::cout << "Output: " << filename << std::endl;
    std::cout << "Resolution: " << WINDOW_WIDTH << "x" << WINDOW_HEIGHT << std::endl;
    std::cout << "FPS: " << fps << std::endl;
    std::cout << "Total frames: " << allFilePaths.size() << " (all files in folder)" << std::endl;
    std::cout << "Source resolution: " << originalImageWidth << "x" << originalImageHeight << std::endl;
    std::cout << "Export threads: " << numExportThreads << std::endl;
    std::cout << "\nMemory usage during export:" << std::endl;
    std::cout << "  Frame buffer queue (max " << maxQueueSize << "): " 
              << (maxQueueSize * frameBufferSize / (1024.0 * 1024.0)) << " MB" << std::endl;
    std::cout << "  Source images in flight (max " << numExportThreads << "): " 
              << (numExportThreads * originalImageSize / (1024.0 * 1024.0)) << " MB" << std::endl;
    std::cout << "  Total peak: ~" 
              << ((maxQueueSize * frameBufferSize + numExportThreads * originalImageSize) / (1024.0 * 1024.0)) 
              << " MB" << std::endl;
    std::cout << "\nStarting export..." << std::endl;
    
    // Build FFmpeg command
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
        "ffmpeg -y -f rawvideo -pixel_format rgb24 -video_size %dx%d -framerate %d -i - "
        "-c:v libx264 -pix_fmt yuv420p -crf 18 \"%s\"",
        WINDOW_WIDTH, WINDOW_HEIGHT, fps, filename);
    
    // Open pipe to FFmpeg
    FILE* ffmpeg = _popen(cmd, "wb");
    if (!ffmpeg) {
        MessageBoxA(g_hwnd, "Failed to start FFmpeg!\nMake sure FFmpeg is installed and in your PATH.", 
                    "Export Error", MB_OK | MB_ICONERROR);
        return;
    }
    
    // Synchronization primitives
    std::mutex queueMutex;
    std::condition_variable queueNotFull;
    std::condition_variable queueNotEmpty;
    std::map<size_t, unsigned char*> renderedFrames;
    std::atomic<size_t> nextFrameToRender(0);
    size_t nextFrameToWrite = 0;
    
    // Capture current view parameters for threads
    double capturedZoom = zoomLevel;
    double capturedPanX = panX;
    double capturedPanY = panY;
    int capturedShrinkFactor = shrinkFactor_global;
    int capturedOrigW = originalImageWidth;
    int capturedOrigH = originalImageHeight;
    int capturedWinW = WINDOW_WIDTH;
    int capturedWinH = WINDOW_HEIGHT;
    size_t totalFrames = allFilePaths.size();
    
    // Worker function for rendering threads
    auto renderWorker = [&]() {
        while (true) {
            size_t frameIdx = nextFrameToRender.fetch_add(1);
            if (frameIdx >= totalFrames) {
                break;
            }
            
            // Allocate buffer for this frame
            unsigned char* buffer = new unsigned char[frameBufferSize];
            memset(buffer, 0, frameBufferSize);
            
            // Load original image
            int w, h, channels;
            unsigned char* originalData = stbi_load(allFilePaths[frameIdx].c_str(), &w, &h, &channels, 3);
            
            if (originalData) {
                // Calculate rendering parameters
                double scaleX = (double)capturedWinW / capturedOrigW;
                double scaleY = (double)capturedWinH / capturedOrigH;
                double fitScale = (scaleX < scaleY) ? scaleX : scaleY;
                double currentScale = fitScale * capturedZoom;
                
                double panXOriginal = capturedPanX * capturedShrinkFactor;
                double panYOriginal = capturedPanY * capturedShrinkFactor;
                
                double centerX = w / 2.0 + panXOriginal;
                double centerY = h / 2.0 + panYOriginal;
                
                // Render to buffer
                for (int outY = 0; outY < capturedWinH; outY++) {
                    for (int outX = 0; outX < capturedWinW; outX++) {
                        double imgX = (outX - capturedWinW / 2.0) / currentScale + centerX;
                        double imgY = (outY - capturedWinH / 2.0) / currentScale + centerY;
                        
                        int srcX = (int)imgX;
                        int srcY = (int)imgY;
                        
                        if (srcX >= 0 && srcX < w && srcY >= 0 && srcY < h) {
                            int srcIdx = (srcY * w + srcX) * 3;
                            int dstIdx = (outY * capturedWinW + outX) * 3;
                            buffer[dstIdx + 0] = originalData[srcIdx + 0];
                            buffer[dstIdx + 1] = originalData[srcIdx + 1];
                            buffer[dstIdx + 2] = originalData[srcIdx + 2];
                        }
                    }
                }
                
                stbi_image_free(originalData);
            }
            
            // Wait until queue has space, then add frame
            {
                std::unique_lock<std::mutex> lock(queueMutex);
                queueNotFull.wait(lock, [&]() {
                    return renderedFrames.size() < maxQueueSize;
                });
                renderedFrames[frameIdx] = buffer;
            }
            queueNotEmpty.notify_one();
        }
    };
    
    // Start timer
    LARGE_INTEGER startTime, currentTime;
    QueryPerformanceCounter(&startTime);
    
    // Start worker threads
    std::vector<std::thread> workers;
    for (int i = 0; i < numExportThreads; i++) {
        workers.emplace_back(renderWorker);
    }
    
    // Main thread: write frames in order
    SetWindowTextA(g_hwnd, "Exporting HQ...");
    while (nextFrameToWrite < totalFrames) {
        unsigned char* frameData = nullptr;
        
        // Wait for the next frame we need
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            queueNotEmpty.wait(lock, [&]() {
                return renderedFrames.find(nextFrameToWrite) != renderedFrames.end();
            });
            
            frameData = renderedFrames[nextFrameToWrite];
            renderedFrames.erase(nextFrameToWrite);
        }
        queueNotFull.notify_one();
        
        // Write to FFmpeg
        fwrite(frameData, 1, frameBufferSize, ffmpeg);
        delete[] frameData;
        
        nextFrameToWrite++;
        
        // Update progress every 10 frames
        if (nextFrameToWrite % 10 == 0 || nextFrameToWrite == totalFrames) {
            double percent = (100.0 * nextFrameToWrite) / totalFrames;
            
            QueryPerformanceCounter(&currentTime);
            double elapsed = (double)(currentTime.QuadPart - startTime.QuadPart) / perfFrequency.QuadPart;
            double fps_actual = nextFrameToWrite / elapsed;
            double eta = (totalFrames - nextFrameToWrite) / fps_actual;
            
            char title[256];
            snprintf(title, sizeof(title), "Exporting HQ: %zu/%zu (%.1f%%) - %.1f fps", 
                     nextFrameToWrite, totalFrames, percent, fps_actual);
            SetWindowTextA(g_hwnd, title);
            
            std::cout << "\r  Frame " << nextFrameToWrite << "/" << totalFrames 
                      << " (" << (int)percent << "%) - " 
                      << std::fixed << std::setprecision(1) << fps_actual << " fps - "
                      << "ETA: " << (int)(eta / 60) << "m " << (int)eta % 60 << "s   " << std::flush;
            
            // Process window messages
            MSG msg;
            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
    }
    
    // Wait for all workers to finish
    for (auto& t : workers) {
        t.join();
    }
    
    std::cout << std::endl;
    _pclose(ffmpeg);
    
    // Calculate total time
    QueryPerformanceCounter(&currentTime);
    double totalTime = (double)(currentTime.QuadPart - startTime.QuadPart) / perfFrequency.QuadPart;
    
    std::cout << "Export complete! Total time: " << (int)(totalTime / 60) << "m " 
              << (int)totalTime % 60 << "s" << std::endl;
    
    UpdateWindowTitle();
    
    char msg[512];
    snprintf(msg, sizeof(msg), 
             "Export complete!\n\n"
             "File: %s\n"
             "Frames: %zu\n"
             "Time: %.1f seconds\n"
             "Avg speed: %.1f fps\n"
             "Threads used: %d",
             filename, totalFrames, totalTime, totalFrames / totalTime, numExportThreads);
    MessageBoxA(g_hwnd, msg, "Export Complete", MB_OK | MB_ICONINFORMATION);
}

// Calculate the base scale to fit image in window
double GetFitScale() {
    double scaleX = (double)WINDOW_WIDTH / imageWidth;
    double scaleY = (double)WINDOW_HEIGHT / imageHeight;
    return (scaleX < scaleY) ? scaleX : scaleY;
}

void ClampPan() {
    double fitScale = GetFitScale();
    double currentScale = fitScale * zoomLevel;
    
    // Calculate how much of the image is visible at current zoom
    double visibleWidth = WINDOW_WIDTH / currentScale;
    double visibleHeight = WINDOW_HEIGHT / currentScale;
    
    // Calculate max pan (half the difference between image size and visible area)
    double maxPanX = (imageWidth - visibleWidth) / 2.0;
    double maxPanY = (imageHeight - visibleHeight) / 2.0;
    
    if (maxPanX < 0) maxPanX = 0;
    if (maxPanY < 0) maxPanY = 0;
    
    if (panX < -maxPanX) panX = -maxPanX;
    if (panX > maxPanX) panX = maxPanX;
    if (panY < -maxPanY) panY = -maxPanY;
    if (panY > maxPanY) panY = maxPanY;
}

void ResetView() {
    zoomLevel = 1.0;
    panX = 0.0;
    panY = 0.0;
}

void UpdateWindowTitle() {
    if (g_hwnd && !frames.empty()) {
        char title[256];
        if (isPlaying) {
            const char* direction = (playDirection > 0) ? "▶" : "◀";
            snprintf(title, sizeof(title), "%s [%d/%d] - %.1f FPS %s", 
                     frames[currentFrame].filename.c_str(),
                     currentFrame + 1, 
                     (int)frames.size(),
                     currentFPS,
                     direction);
        } else {
            snprintf(title, sizeof(title), "%s [%d/%d] - Zoom: %.0f%%", 
                     frames[currentFrame].filename.c_str(),
                     currentFrame + 1, 
                     (int)frames.size(),
                     zoomLevel * 100.0);
        }
        SetWindowTextA(g_hwnd, title);
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            
            // Double buffering to prevent flicker
            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBitmap = CreateCompatibleBitmap(hdc, WINDOW_WIDTH, WINDOW_HEIGHT);
            HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);
            
            // Clear background on memory DC
            RECT clientRect = {0, 0, WINDOW_WIDTH, WINDOW_HEIGHT};
            FillRect(memDC, &clientRect, (HBRUSH)GetStockObject(BLACK_BRUSH));
            
            if (!frames.empty() && frames[currentFrame].data && bmi) {
                double fitScale = GetFitScale();
                double currentScale = fitScale * zoomLevel;
                
                // Calculate source rectangle (what part of the image to show)
                double visibleWidth = WINDOW_WIDTH / currentScale;
                double visibleHeight = WINDOW_HEIGHT / currentScale;
                
                // Center of the view in image coordinates
                double centerX = imageWidth / 2.0 + panX;
                double centerY = imageHeight / 2.0 + panY;
                
                // Source rectangle
                int srcX = (int)(centerX - visibleWidth / 2.0);
                int srcY = (int)(centerY - visibleHeight / 2.0);
                int srcW = (int)visibleWidth;
                int srcH = (int)visibleHeight;
                
                // Clamp source to image bounds
                if (srcX < 0) srcX = 0;
                if (srcY < 0) srcY = 0;
                if (srcX + srcW > imageWidth) srcW = imageWidth - srcX;
                if (srcY + srcH > imageHeight) srcH = imageHeight - srcY;
                
                // Calculate destination rectangle (where to draw on screen)
                int dstX = (int)((srcX - (centerX - visibleWidth / 2.0)) * currentScale);
                int dstY = (int)((srcY - (centerY - visibleHeight / 2.0)) * currentScale);
                int dstW = (int)(srcW * currentScale);
                int dstH = (int)(srcH * currentScale);
                
                // Flip srcY for bottom-up DIB
                int flippedSrcY = imageHeight - srcY - srcH;
                
                SetStretchBltMode(memDC, HALFTONE);
                StretchDIBits(memDC,
                    dstX, dstY, dstW, dstH,           // destination
                    srcX, flippedSrcY, srcW, srcH,    // source
                    frames[currentFrame].data, bmi, DIB_RGB_COLORS, SRCCOPY);
            }
            
            // Copy the buffer to the screen
            BitBlt(hdc, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, memDC, 0, 0, SRCCOPY);
            
            // Cleanup
            SelectObject(memDC, oldBitmap);
            DeleteObject(memBitmap);
            DeleteDC(memDC);
            
            EndPaint(hwnd, &ps);
            return 0;
        }
        
        case WM_ERASEBKGND:
            // Prevent background erase to avoid flicker
            return 1;
        
        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            double zoomFactor = (delta > 0) ? 1.15 : (1.0 / 1.15);
            
            // Get mouse position relative to window
            POINT pt;
            pt.x = GET_X_LPARAM(lParam);
            pt.y = GET_Y_LPARAM(lParam);
            ScreenToClient(hwnd, &pt);
            
            double fitScale = GetFitScale();
            double oldScale = fitScale * zoomLevel;
            
            // Calculate mouse position in image coordinates before zoom
            double mouseImgX = (pt.x - WINDOW_WIDTH / 2.0) / oldScale + imageWidth / 2.0 + panX;
            double mouseImgY = (pt.y - WINDOW_HEIGHT / 2.0) / oldScale + imageHeight / 2.0 + panY;
            
            // Apply zoom
            zoomLevel *= zoomFactor;
            if (zoomLevel < minZoom) zoomLevel = minZoom;
            if (zoomLevel > maxZoom) zoomLevel = maxZoom;
            
            double newScale = fitScale * zoomLevel;
            
            // Adjust pan so mouse stays over the same image point
            panX = mouseImgX - imageWidth / 2.0 - (pt.x - WINDOW_WIDTH / 2.0) / newScale;
            panY = mouseImgY - imageHeight / 2.0 - (pt.y - WINDOW_HEIGHT / 2.0) / newScale;
            
            ClampPan();
            InvalidateRect(hwnd, NULL, FALSE);
            UpdateWindowTitle();
            return 0;
        }
        
        case WM_LBUTTONDOWN: {
            isDragging = true;
            lastMouseX = GET_X_LPARAM(lParam);
            lastMouseY = GET_Y_LPARAM(lParam);
            SetCapture(hwnd);
            return 0;
        }
        
        case WM_LBUTTONUP: {
            isDragging = false;
            ReleaseCapture();
            return 0;
        }
        
        case WM_MOUSEMOVE: {
            if (isDragging) {
                int mouseX = GET_X_LPARAM(lParam);
                int mouseY = GET_Y_LPARAM(lParam);
                
                double fitScale = GetFitScale();
                double currentScale = fitScale * zoomLevel;
                
                // Convert pixel movement to image coordinate movement
                double dx = (lastMouseX - mouseX) / currentScale;
                double dy = (lastMouseY - mouseY) / currentScale;
                
                panX += dx;
                panY += dy;
                
                ClampPan();
                
                lastMouseX = mouseX;
                lastMouseY = mouseY;
                
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }
        
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
            
        case WM_KEYDOWN:
            if (wParam == 'Q') {
                PostQuitMessage(0);
            }
            else if (wParam == VK_ESCAPE) {
                // Open folder selection dialog
                isPlaying = false;
                std::string newFolder = SelectFolder(currentFolder);
                if (!newFolder.empty() && newFolder != currentFolder) {
                    currentFolder = newFolder;
                    needsReload = true;
                }
            }
            else if (wParam == VK_LEFT || wParam == VK_UP || wParam == 'A') {
                if (currentFrame > 0) {
                    currentFrame--;
                    InvalidateRect(hwnd, NULL, FALSE);
                    UpdateWindowTitle();
                }
            }
            else if (wParam == VK_RIGHT || wParam == VK_DOWN || wParam == 'D') {
                if (currentFrame < (int)frames.size() - 1) {
                    currentFrame++;
                    InvalidateRect(hwnd, NULL, FALSE);
                    UpdateWindowTitle();
                }
            }
            else if (wParam == VK_HOME) {
                currentFrame = 0;
                InvalidateRect(hwnd, NULL, FALSE);
                UpdateWindowTitle();
            }
            else if (wParam == VK_END) {
                currentFrame = (int)frames.size() - 1;
                InvalidateRect(hwnd, NULL, FALSE);
                UpdateWindowTitle();
            }
            else if (wParam == 'R') {
                // Reset zoom and pan
                ResetView();
                InvalidateRect(hwnd, NULL, FALSE);
                UpdateWindowTitle();
            }
            else if (wParam == VK_SPACE) {
                // Toggle playback
                isPlaying = !isPlaying;
                if (isPlaying) {
                    QueryPerformanceCounter(&lastFrameTime);
                    frameCount = 0;
                    fpsAccumulator = 0.0;
                }
                UpdateWindowTitle();
            }
            else if (wParam == 'J') {
                // Reverse playback direction
                playDirection = -playDirection;
                UpdateWindowTitle();
            }
            else if (wParam == 'S') {
                // Export to MP4
                isPlaying = false;
                exportRequested = true;
            }
            else if (wParam == 'E') {
                // Export current view settings to file
                char filename[MAX_PATH] = "export_settings.txt";
                OPENFILENAMEA ofn = {0};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hwnd;
                ofn.lpstrFilter = "Text Files\0*.txt\0All Files\0*.*\0";
                ofn.lpstrFile = filename;
                ofn.nMaxFile = MAX_PATH;
                ofn.lpstrTitle = "Export View Settings";
                ofn.Flags = OFN_OVERWRITEPROMPT;
                ofn.lpstrDefExt = "txt";
                
                if (GetSaveFileNameA(&ofn)) {
                    FILE* f = fopen(filename, "a");  // Append mode for batch
                    if (f) {
                        fprintf(f, "output.mp4|%.6f|%.6f|%.6f|%d|%d|30\n",
                                zoomLevel, panX, panY, 0, (int)frames.size() - 1);
                        fclose(f);
                        MessageBoxA(hwnd, "View settings exported!\nEdit the file to customize output name, frame range, and FPS.", 
                                   "Settings Exported", MB_OK | MB_ICONINFORMATION);
                    }
                }
            }
            return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

// Find all matching PNG files
std::vector<std::string> FindMatchingFiles(const std::string& pattern) {
    std::vector<std::string> files;
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(pattern.c_str(), &findData);
    
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                files.push_back(findData.cFileName);
            }
        } while (FindNextFileA(hFind, &findData));
        FindClose(hFind);
    }
    
    return files;
}

// Extract the numeric index from filename like "e_png_yx_0.5_000100.png" or "image_123.png"
// Matches pattern *_<number>.png
int ExtractIndex(const std::string& filename) {
    size_t dotPos = filename.rfind('.');
    if (dotPos == std::string::npos) return -1;
    
    // Find the last underscore before the extension
    size_t lastUnderscore = filename.rfind('_', dotPos);
    if (lastUnderscore == std::string::npos) return -1;
    
    std::string numStr = filename.substr(lastUnderscore + 1, dotPos - lastUnderscore - 1);
    
    // Verify it's all digits
    for (char c : numStr) {
        if (!isdigit(c)) return -1;
    }
    
    try {
        return std::stoi(numStr);
    } catch (...) {
        return -1;
    }
}

// Load and shrink a single image
unsigned char* LoadAndShrinkImage(const std::string& filename, int shrinkFactor, 
                                   int& outWidth, int& outHeight) {
    int w, h, channels;
    unsigned char* originalData = stbi_load(filename.c_str(), &w, &h, &channels, 3);
    
    if (!originalData) {
        std::cerr << "Error loading: " << filename << " - " << stbi_failure_reason() << std::endl;
        return nullptr;
    }
    
    int newWidth = w / shrinkFactor;
    int newHeight = h / shrinkFactor;
    
    // Convert RGB to BGR for Windows (and flip vertically for DIB)
    unsigned char* bgrData = new unsigned char[newWidth * newHeight * 3];
    for (int y = 0; y < newHeight; y++) {
        for (int x = 0; x < newWidth; x++) {
            int srcX = x * shrinkFactor;
            int srcY = y * shrinkFactor;
            int srcIdx = (srcY * w + srcX) * 3;
            int dstIdx = ((newHeight - 1 - y) * newWidth + x) * 3;
            bgrData[dstIdx + 0] = originalData[srcIdx + 2];  // B
            bgrData[dstIdx + 1] = originalData[srcIdx + 1];  // G
            bgrData[dstIdx + 2] = originalData[srcIdx + 0];  // R
        }
    }
    
    // Free original data immediately
    stbi_image_free(originalData);
    
    outWidth = newWidth;
    outHeight = newHeight;
    return bgrData;
}

// Open folder selection dialog
std::string SelectFolder(const std::string& initialFolder) {
    char path[MAX_PATH] = {0};
    
    BROWSEINFOA bi = {0};
    bi.hwndOwner = g_hwnd;
    bi.lpszTitle = "Select folder containing PNG images (e_png_yx_0.5_*.png)";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    
    // Set initial folder if provided
    if (!initialFolder.empty()) {
        bi.lParam = (LPARAM)initialFolder.c_str();
        bi.lpfn = [](HWND hwnd, UINT uMsg, LPARAM lParam, LPARAM lpData) -> int {
            if (uMsg == BFFM_INITIALIZED && lpData) {
                SendMessage(hwnd, BFFM_SETSELECTION, TRUE, lpData);
            }
            return 0;
        };
    }
    
    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (pidl && SHGetPathFromIDListA(pidl, path)) {
        CoTaskMemFree(pidl);
        return std::string(path);
    }
    if (pidl) CoTaskMemFree(pidl);
    return "";
}

// Cleanup existing frames
void CleanupFrames() {
    for (auto& frame : frames) {
        if (frame.data) {
            delete[] frame.data;
            frame.data = nullptr;
        }
    }
    frames.clear();
    allFilePaths.clear();
    currentFrame = 0;
    imageWidth = 0;
    imageHeight = 0;
    originalImageWidth = 0;
    originalImageHeight = 0;
}

// Load images from a folder
bool LoadImagesFromFolder(const std::string& folder) {
    int shrinkFactor = shrinkFactor_global;
    const int numThreads = numThreads_global;
    const int nthFrame = nthFrame_global;
    
    // Save current directory and change to target folder
    char oldDir[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, oldDir);
    SetCurrentDirectoryA(folder.c_str());
    
    // Clean up existing frames
    CleanupFrames();
    
    // Find all PNG files matching pattern *_<number>.png
    std::vector<std::string> allFiles = FindMatchingFiles("*.png");
    
    // Filter files that match the *_<number>.png pattern
    std::vector<std::pair<std::string, int>> validFiles;
    for (const auto& file : allFiles) {
        int idx = ExtractIndex(file);
        if (idx >= 0) {
            validFiles.push_back({file, idx});
        }
    }
    
    // Sort by numeric index
    std::sort(validFiles.begin(), validFiles.end(), 
        [](const std::pair<std::string, int>& a, const std::pair<std::string, int>& b) { 
            return a.second < b.second; 
        });
    
    // Restore directory temporarily to probe image dimensions
    SetCurrentDirectoryA(oldDir);
    
    if (validFiles.empty()) {
        std::cerr << "No matching files found in " << folder << std::endl;
        MessageBoxA(g_hwnd, "No matching PNG files found (*_<number>.png pattern)", "No Images Found", MB_OK | MB_ICONWARNING);
        return false;
    }
    
    // Auto-calculate shrink factor if not specified
    if (shrinkFactor == 0) {
        // Probe first image to get dimensions
        std::string probeFile = folder + "\\" + validFiles[0].first;
        int probeW, probeH, probeChannels;
        if (stbi_info(probeFile.c_str(), &probeW, &probeH, &probeChannels)) {
            // Target: preview image should be ~2x window size
            // shrinkFactor = originalSize / targetSize
            // targetSize = 2 * windowSize
            // shrinkFactor = originalSize / (2 * windowSize)
            int targetW = WINDOW_WIDTH * 2;
            int targetH = WINDOW_HEIGHT * 2;
            
            int shrinkX = probeW / targetW;
            int shrinkY = probeH / targetH;
            shrinkFactor = (shrinkX > shrinkY) ? shrinkX : shrinkY;
            
            // Minimum shrink factor is 1 (full resolution)
            if (shrinkFactor < 1) shrinkFactor = 1;
            
            std::cout << "Original image size: " << probeW << " x " << probeH << std::endl;
            std::cout << "Auto shrink factor: " << shrinkFactor << " (preview ~" 
                      << (probeW / shrinkFactor) << " x " << (probeH / shrinkFactor) << ")" << std::endl;
        } else {
            std::cerr << "Could not probe image dimensions, using shrink factor 4" << std::endl;
            shrinkFactor = 4;
        }
    }
    
    // Select every n-th file, always including first and last
    std::vector<std::string> files;
    for (size_t i = 0; i < validFiles.size(); i += nthFrame) {
        files.push_back(validFiles[i].first);
    }
    // Always include the last frame if not already included
    if (!validFiles.empty() && (validFiles.size() - 1) % nthFrame != 0) {
        files.push_back(validFiles.back().first);
    }
    
    // Store ALL file paths for high-quality export (sorted by index)
    allFilePaths.clear();
    for (const auto& vf : validFiles) {
        allFilePaths.push_back(folder + "\\" + vf.first);
    }
    
    std::cout << "\nFolder: " << folder << std::endl;
    std::cout << "Found " << validFiles.size() << " matching images (*_<number>.png)" << std::endl;
    if (nthFrame > 1) {
        std::cout << "Loading every " << nthFrame << "-th image: " << files.size() << " images" << std::endl;
    }
    std::cout << "Loading with " << numThreads << " threads..." << std::endl;
    
    // Prepare frames vector with correct size
    frames.resize(files.size());
    std::mutex progressMutex;
    int loadedCount = 0;
    int firstWidth = 0, firstHeight = 0;
    std::mutex dimMutex;
    
    // Prepend folder path to filenames
    std::vector<std::string> fullPaths;
    for (const auto& file : files) {
        fullPaths.push_back(folder + "\\" + file);
    }
    
    // Worker function to load a range of images
    auto loadWorker = [&](size_t startIdx, size_t endIdx) {
        for (size_t i = startIdx; i < endIdx; i++) {
            int w, h;
            unsigned char* data = LoadAndShrinkImage(fullPaths[i], shrinkFactor, w, h);
            
            if (data) {
                // Store first dimensions (thread-safe)
                {
                    std::lock_guard<std::mutex> lock(dimMutex);
                    if (firstWidth == 0) {
                        firstWidth = w;
                        firstHeight = h;
                    }
                }
                
                frames[i].filename = files[i];
                frames[i].index = ExtractIndex(files[i]);
                frames[i].data = data;
            } else {
                frames[i].data = nullptr;
            }
            
            // Update progress
            {
                std::lock_guard<std::mutex> lock(progressMutex);
                loadedCount++;
                std::cout << "\rLoading: " << loadedCount << "/" << files.size() << std::flush;
            }
        }
    };
    
    // Create and start threads
    std::vector<std::thread> threads;
    size_t filesPerThread = (files.size() + numThreads - 1) / numThreads;
    
    for (int t = 0; t < numThreads; t++) {
        size_t startIdx = t * filesPerThread;
        size_t endIdx = std::min(startIdx + filesPerThread, files.size());
        if (startIdx < files.size()) {
            threads.emplace_back(loadWorker, startIdx, endIdx);
        }
    }
    
    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }
    std::cout << std::endl;
    
    // Remove failed loads (nullptr data)
    frames.erase(std::remove_if(frames.begin(), frames.end(), 
        [](const ImageFrame& f) { return f.data == nullptr; }), frames.end());
    
    imageWidth = firstWidth;
    imageHeight = firstHeight;
    
    // Store original (non-shrunk) dimensions for high-quality export
    originalImageWidth = firstWidth * shrinkFactor;
    originalImageHeight = firstHeight * shrinkFactor;
    
    if (frames.empty()) {
        std::cerr << "No images could be loaded" << std::endl;
        return false;
    }
    
    // Sort frames by their numeric index
    std::sort(frames.begin(), frames.end(), [](const ImageFrame& a, const ImageFrame& b) {
        return a.index < b.index;
    });
    
    // Update BITMAPINFO if it exists
    if (bmi) {
        bmi->bmiHeader.biWidth = imageWidth;
        bmi->bmiHeader.biHeight = imageHeight;
    }
    
    // Calculate and print memory usage
    size_t bytesPerImage = (size_t)imageWidth * imageHeight * 3;
    size_t totalBytes = bytesPerImage * frames.size();
    
    std::cout << "\nMemory usage:" << std::endl;
    std::cout << "  Shrink factor used: " << shrinkFactor << std::endl;
    std::cout << "  Preview dimensions: " << imageWidth << " x " << imageHeight << std::endl;
    std::cout << "  Original dimensions: " << originalImageWidth << " x " << originalImageHeight << std::endl;
    if (bytesPerImage < 1024) {
        std::cout << "  RAM per image: " << bytesPerImage << " bytes" << std::endl;
    } else if (bytesPerImage < 1024 * 1024) {
        std::cout << "  RAM per image: " << (bytesPerImage / 1024.0) << " KB" << std::endl;
    } else {
        std::cout << "  RAM per image: " << (bytesPerImage / (1024.0 * 1024.0)) << " MB" << std::endl;
    }
    
    if (totalBytes < 1024) {
        std::cout << "  Total RAM for " << frames.size() << " images: " << totalBytes << " bytes" << std::endl;
    } else if (totalBytes < 1024 * 1024) {
        std::cout << "  Total RAM for " << frames.size() << " images: " << (totalBytes / 1024.0) << " KB" << std::endl;
    } else if (totalBytes < 1024 * 1024 * 1024) {
        std::cout << "  Total RAM for " << frames.size() << " images: " << (totalBytes / (1024.0 * 1024.0)) << " MB" << std::endl;
    } else {
        std::cout << "  Total RAM for " << frames.size() << " images: " << (totalBytes / (1024.0 * 1024.0 * 1024.0)) << " GB" << std::endl;
    }
    
    std::cout << "\nLoaded " << frames.size() << " images for preview" << std::endl;
    std::cout << "Export will use all " << allFilePaths.size() << " files at full resolution" << std::endl;
    
    // Reset view
    ResetView();
    currentFrame = 0;
    isPlaying = false;
    
    return true;
}

int main(int argc, char* argv[]) {
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--shrink") == 0) && i + 1 < argc) {
            shrinkFactor_global = atoi(argv[i + 1]);
            if (shrinkFactor_global < 1) shrinkFactor_global = 1;
            i++;  // Skip next argument
        }
        else if ((strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--nth") == 0) && i + 1 < argc) {
            nthFrame_global = atoi(argv[i + 1]);
            if (nthFrame_global < 1) nthFrame_global = 1;
            i++;  // Skip next argument
        }
        else if (strcmp(argv[i], "-x") == 0 && i + 1 < argc) {
            WINDOW_WIDTH = atoi(argv[i + 1]);
            if (WINDOW_WIDTH < 100) WINDOW_WIDTH = 100;
            if (WINDOW_WIDTH > 7680) WINDOW_WIDTH = 7680;  // 8K max
            i++;  // Skip next argument
        }
        else if (strcmp(argv[i], "-y") == 0 && i + 1 < argc) {
            WINDOW_HEIGHT = atoi(argv[i + 1]);
            if (WINDOW_HEIGHT < 100) WINDOW_HEIGHT = 100;
            if (WINDOW_HEIGHT > 4320) WINDOW_HEIGHT = 4320;  // 8K max
            i++;  // Skip next argument
        }
        else if ((strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--threads") == 0) && i + 1 < argc) {
            numThreads_global = atoi(argv[i + 1]);
            if (numThreads_global < 1) numThreads_global = 1;
            if (numThreads_global > 64) numThreads_global = 64;
            i++;  // Skip next argument
        }
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  -s, --shrink <factor>  Shrink factor for images (default: auto)" << std::endl;
            std::cout << "                         Auto = image is ~2x window size, or full res if smaller" << std::endl;
            std::cout << "  -n, --nth <n>          Load every n-th image (default: 1 = all)" << std::endl;
            std::cout << "  -x <width>             Window width in pixels (default: 1000)" << std::endl;
            std::cout << "  -y <height>            Window height in pixels (default: 1000)" << std::endl;
            std::cout << "  -t, --threads <n>      Number of threads for loading/export (default: 12)" << std::endl;
            std::cout << "  -h, --help             Show this help message" << std::endl;
            std::cout << "\nImage files must match pattern: *_<number>.png" << std::endl;
            std::cout << "Examples: image_001.png, frame_12345.png, e_png_yx_0.5_000100.png" << std::endl;
            return 0;
        }
    }
    
    // Initialize COM for folder dialog
    CoInitialize(NULL);
    
    std::cout << "PNG Image Viewer" << std::endl;
    std::cout << "================" << std::endl;
    if (shrinkFactor_global == 0) {
        std::cout << "\nShrink factor: auto (image ~2x window size)" << std::endl;
    } else {
        std::cout << "\nShrink factor: " << shrinkFactor_global << std::endl;
    }
    std::cout << "Load every " << nthFrame_global << "-th image" << std::endl;
    std::cout << "Window resolution: " << WINDOW_WIDTH << " x " << WINDOW_HEIGHT << std::endl;
    std::cout << "Threads: " << numThreads_global << std::endl;
    std::cout << "Image pattern: *_<number>.png" << std::endl;
    std::cout << "\nControls:" << std::endl;
    std::cout << "  Left/Up Arrow: Previous image" << std::endl;
    std::cout << "  Right/Down Arrow: Next image" << std::endl;
    std::cout << "  Home: First image" << std::endl;
    std::cout << "  End: Last image" << std::endl;
    std::cout << "  Space: Play/Pause animation" << std::endl;
    std::cout << "  J: Reverse playback direction" << std::endl;
    std::cout << "  S: Export current view to MP4" << std::endl;
    std::cout << "  E: Export view settings to file (for batch rendering)" << std::endl;
    std::cout << "  Mouse Wheel: Zoom in/out" << std::endl;
    std::cout << "  Left Mouse Drag: Pan" << std::endl;
    std::cout << "  R: Reset zoom/pan" << std::endl;
    std::cout << "  ESC: Change folder" << std::endl;
    std::cout << "  Q: Quit" << std::endl;
    
    // Select initial folder
    char currentDir[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, currentDir);
    currentFolder = SelectFolder(currentDir);
    
    if (currentFolder.empty()) {
        std::cerr << "No folder selected. Exiting." << std::endl;
        CoUninitialize();
        return -1;
    }
    
    // Load images from selected folder
    if (!LoadImagesFromFolder(currentFolder)) {
        CoUninitialize();
        return -1;
    }
    
    // Setup BITMAPINFO
    bmi = (BITMAPINFO*)malloc(sizeof(BITMAPINFOHEADER));
    memset(bmi, 0, sizeof(BITMAPINFOHEADER));
    bmi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi->bmiHeader.biWidth = imageWidth;
    bmi->bmiHeader.biHeight = imageHeight;
    bmi->bmiHeader.biPlanes = 1;
    bmi->bmiHeader.biBitCount = 24;
    bmi->bmiHeader.biCompression = BI_RGB;
    
    // Register window class
    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "ImageViewer";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClass(&wc);
    
    // Calculate window size (add borders for fixed client area)
    RECT rect = {0, 0, WINDOW_WIDTH, WINDOW_HEIGHT};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    int windowWidth = rect.right - rect.left;
    int windowHeight = rect.bottom - rect.top;
    
    // Create the window
    g_hwnd = CreateWindowEx(
        0,
        "ImageViewer",
        "Image Viewer",
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,  // Fixed size window
        CW_USEDEFAULT, CW_USEDEFAULT,
        windowWidth, windowHeight,
        NULL, NULL, GetModuleHandle(NULL), NULL
    );
    
    if (!g_hwnd) {
        std::cerr << "Error: Could not create window" << std::endl;
        for (auto& frame : frames) {
            delete[] frame.data;
        }
        free(bmi);
        return -1;
    }
    
    UpdateWindowTitle();
    ShowWindow(g_hwnd, SW_SHOW);
    
    // Initialize performance counter for FPS measurement
    QueryPerformanceFrequency(&perfFrequency);
    QueryPerformanceCounter(&lastFrameTime);
    
    // Message loop with playback support
    MSG msg = {};
    while (true) {
        // Process all pending messages
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                goto cleanup;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        
        // Handle folder reload request
        if (needsReload) {
            needsReload = false;
            if (LoadImagesFromFolder(currentFolder)) {
                InvalidateRect(g_hwnd, NULL, FALSE);
                UpdateWindowTitle();
            }
        }
        
        // Handle export request
        if (exportRequested) {
            exportRequested = false;
            ExportToMP4();
        }
        
        // Handle playback
        if (isPlaying) {
            // Advance frame
            int nextFrame = currentFrame + playDirection;
            
            // Wrap around at boundaries
            if (nextFrame >= (int)frames.size()) {
                nextFrame = 0;
            } else if (nextFrame < 0) {
                nextFrame = (int)frames.size() - 1;
            }
            
            currentFrame = nextFrame;
            
            // Calculate FPS
            LARGE_INTEGER currentTime;
            QueryPerformanceCounter(&currentTime);
            double deltaTime = (double)(currentTime.QuadPart - lastFrameTime.QuadPart) / perfFrequency.QuadPart;
            lastFrameTime = currentTime;
            
            frameCount++;
            fpsAccumulator += deltaTime;
            
            // Update FPS display every 10 frames for smoother reading
            if (frameCount >= 10) {
                currentFPS = frameCount / fpsAccumulator;
                frameCount = 0;
                fpsAccumulator = 0.0;
            }
            
            InvalidateRect(g_hwnd, NULL, FALSE);
            UpdateWindow(g_hwnd);  // Force immediate redraw
            UpdateWindowTitle();
        } else {
            // When not playing, wait for messages to save CPU
            WaitMessage();
        }
    }
    
cleanup:
    
    // Cleanup
    CleanupFrames();
    free(bmi);
    CoUninitialize();
    
    return 0;
}
