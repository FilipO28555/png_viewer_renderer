// Image loading utilities for PNG Image Viewer
// Platform-independent image loading with stb_image

#ifndef IMAGE_LOADER_H
#define IMAGE_LOADER_H

#include "frame_types.h"
#include <string>
#include <vector>
#include <functional>

// Extract the numeric index from filename like "e_png_yx_0.5_000100.png"
// Matches pattern *_<number>.png
int ExtractIndex(const std::string& filename);

// Load and shrink a single image
// Returns RGB data (for Linux/SDL2) when rgbOutput=true, BGR (for Windows) when false
unsigned char* LoadAndShrinkImage(const std::string& filename, int shrinkFactor, 
                                   int& outWidth, int& outHeight,
                                   bool rgbOutput = true, bool flipVertical = false);

// Auto-calculate shrink factor based on image and window dimensions
int AutoCalculateShrinkFactor(const std::string& probeFilePath, int windowWidth, int windowHeight);

// Progress callback: (current, total) -> should_continue
using ProgressCallback = std::function<bool(int current, int total)>;

// Load images from a folder - platform independent parts
// Platform-specific code should handle file enumeration and pass file list here
bool LoadImagesCommon(
    ImageCollection& collection,
    const std::vector<std::string>& files,          // Already sorted file paths
    const std::vector<std::string>& allFilePaths,   // All files for export
    const std::string& folder,
    int shrinkFactor,
    int numThreads,
    bool rgbOutput,         // true for Linux/SDL2, false for Windows
    bool flipVertical,      // true for Windows GDI (bottom-up DIB)
    ProgressCallback progressCallback = nullptr
);

#endif // IMAGE_LOADER_H
