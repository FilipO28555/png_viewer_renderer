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
    bool mode3D = false;        // 3D mode: folder contains z-subfolders
    bool debugMode = false;     // Show debug output
    
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
    
    // 3D mode: z-height navigation
    std::vector<int> zHeights;           // Available z-heights (sorted)
    int currentZIndex = 0;               // Index into zHeights vector
    std::vector<std::vector<std::string>> zAllFilePaths;  // Per z-height file paths
    std::vector<std::vector<ImageFrame>> zFrames;  // Per z-height loaded frames (all in memory)
    bool using3DMode = false;  // Flag to indicate frames points into zFrames (don't double-free)
    
    bool isEmpty() const { 
        if (using3DMode && currentZIndex < (int)zFrames.size()) {
            return zFrames[currentZIndex].empty();
        }
        return frames.empty(); 
    }
    
    size_t size() const { 
        if (using3DMode && currentZIndex < (int)zFrames.size()) {
            return zFrames[currentZIndex].size();
        }
        return frames.size(); 
    }
    
    void cleanup() {
        // In 3D mode, only cleanup zFrames (frames is just a copy/reference)
        if (using3DMode) {
            for (auto& zFrameList : zFrames) {
                for (auto& frame : zFrameList) {
                    if (frame.data) {
                        delete[] frame.data;
                        frame.data = nullptr;
                    }
                }
            }
            frames.clear(); // Just clear the vector, don't delete data
        } else {
            // In 2D mode, cleanup frames normally
            for (auto& frame : frames) {
                if (frame.data) {
                    delete[] frame.data;
                    frame.data = nullptr;
                }
            }
        }
        
        frames.clear();
        allFilePaths.clear();
        currentFrame = 0;
        imageWidth = 0;
        imageHeight = 0;
        originalImageWidth = 0;
        originalImageHeight = 0;
        zHeights.clear();
        currentZIndex = 0;
        zAllFilePaths.clear();
        zFrames.clear();
        using3DMode = false;
    }
};

#endif // FRAME_TYPES_H
