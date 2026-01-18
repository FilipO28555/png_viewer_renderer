// Common data structures for PNG Image Viewer
// Platform-independent types shared between Windows and Linux

#ifndef FRAME_TYPES_H
#define FRAME_TYPES_H

#include <string>
#include <vector>

// Structure to hold image data
struct ImageFrame {
    std::string filename;
    int index;              // The numeric part (e.g., 000100 -> 100)
    unsigned char* data;
    
    ImageFrame() : index(0), data(nullptr) {}
};

// Application settings (can be set via command line)
struct AppSettings {
    int windowWidth = 1000;
    int windowHeight = 1000;
    int shrinkFactor = 0;       // 0 = auto-calculate based on window size
    int nthFrame = 1;           // Load every n-th frame (1 = all frames)
    int numThreads = 72;        // Number of threads for loading and export
    std::string initialFolder;  // Starting folder (empty = prompt or current dir)
    
    // Zoom limits
    double minZoom = 1.0;
    double maxZoom = 10.0;
};

// View state (zoom, pan, playback)
struct ViewState {
    double zoomLevel = 1.0;     // 1.0 = fit to window
    double panX = 0.0;          // Pan offset in image coordinates
    double panY = 0.0;
    bool isDragging = false;
    int lastMouseX = 0;
    int lastMouseY = 0;
    
    // Playback
    bool isPlaying = false;
    int playDirection = 1;      // 1 = forward, -1 = backward
    double currentFPS = 0.0;
    int frameCount = 0;
    double fpsAccumulator = 0.0;
    
    void reset() {
        zoomLevel = 1.0;
        panX = 0.0;
        panY = 0.0;
    }
};

// Image collection state
struct ImageCollection {
    std::vector<ImageFrame> frames;
    std::vector<std::string> allFilePaths;  // All files for full-quality export
    int currentFrame = 0;
    int imageWidth = 0;
    int imageHeight = 0;
    int originalImageWidth = 0;   // Original (non-shrunk) dimensions
    int originalImageHeight = 0;
    std::string currentFolder;
    
    bool isEmpty() const { return frames.empty(); }
    size_t size() const { return frames.size(); }
    
    void cleanup() {
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
};

#endif // FRAME_TYPES_H
