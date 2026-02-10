// Image loading utilities implementation
// Platform-independent image loading with stb_image

#include "image_loader.h"
#include "stb_image.h"
#include <iostream>
#include <thread>
#include <mutex>
#include <algorithm>
#include <cctype>

// Global interrupt flag - can be set by signal handler
std::atomic<bool> g_interrupted(false);

int ExtractIndex(const std::string& filename) {
    size_t dotPos = filename.rfind('.');
    if (dotPos == std::string::npos) return -1;
    
    // Find the last underscore before the extension
    size_t lastUnderscore = filename.rfind('_', dotPos);
    if (lastUnderscore == std::string::npos) return -1;
    
    std::string numStr = filename.substr(lastUnderscore + 1, dotPos - lastUnderscore - 1);
    
    // Verify it's all digits
    for (char c : numStr) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return -1;
    }
    
    try {
        return std::stoi(numStr);
    } catch (...) {
        return -1;
    }
}

unsigned char* LoadAndShrinkImage(const std::string& filename, int shrinkFactor, 
                                   int& outWidth, int& outHeight,
                                   bool rgbOutput, bool flipVertical) {
    int w, h, channels;
    unsigned char* originalData = stbi_load(filename.c_str(), &w, &h, &channels, 3);
    
    if (!originalData) {
        std::cerr << "Error loading: " << filename << " - " << stbi_failure_reason() << std::endl;
        return nullptr;
    }
    
    int newWidth = w / shrinkFactor;
    int newHeight = h / shrinkFactor;
    
    unsigned char* outputData = new unsigned char[newWidth * newHeight * 3];
    
    for (int y = 0; y < newHeight; y++) {
        for (int x = 0; x < newWidth; x++) {
            int srcX = x * shrinkFactor;
            int srcY = y * shrinkFactor;
            int srcIdx = (srcY * w + srcX) * 3;
            
            int dstY = flipVertical ? (newHeight - 1 - y) : y;
            int dstIdx = (dstY * newWidth + x) * 3;
            
            if (rgbOutput) {
                // RGB output (Linux/SDL2)
                outputData[dstIdx + 0] = originalData[srcIdx + 0];  // R
                outputData[dstIdx + 1] = originalData[srcIdx + 1];  // G
                outputData[dstIdx + 2] = originalData[srcIdx + 2];  // B
            } else {
                // BGR output (Windows GDI)
                outputData[dstIdx + 0] = originalData[srcIdx + 2];  // B
                outputData[dstIdx + 1] = originalData[srcIdx + 1];  // G
                outputData[dstIdx + 2] = originalData[srcIdx + 0];  // R
            }
        }
    }
    
    stbi_image_free(originalData);
    
    outWidth = newWidth;
    outHeight = newHeight;
    return outputData;
}

int AutoCalculateShrinkFactor(const std::string& probeFilePath, int windowWidth, int windowHeight) {
    int probeW, probeH, probeChannels;
    if (stbi_info(probeFilePath.c_str(), &probeW, &probeH, &probeChannels)) {
        // Target: preview image should be ~2x window size
        int targetW = windowWidth * 2;
        int targetH = windowHeight * 2;
        
        int shrinkX = probeW / targetW;
        int shrinkY = probeH / targetH;
        int shrinkFactor = std::max(shrinkX, shrinkY);
        
        // Minimum shrink factor is 1 (full resolution)
        if (shrinkFactor < 1) shrinkFactor = 1;
        
        std::cout << "Original image size: " << probeW << " x " << probeH << std::endl;
        std::cout << "Auto shrink factor: " << shrinkFactor << " (preview ~" 
                  << (probeW / shrinkFactor) << " x " << (probeH / shrinkFactor) << ")" << std::endl;
        
        return shrinkFactor;
    }
    
    std::cerr << "Could not probe image dimensions, using shrink factor 4" << std::endl;
    return 4;
}

bool LoadImagesCommon(
    ImageCollection& collection,
    const std::vector<std::string>& files,
    const std::vector<std::string>& allFilePaths,
    const std::string& folder,
    int shrinkFactor,
    int numThreads,
    bool rgbOutput,
    bool flipVertical,
    ProgressCallback progressCallback
) {
    if (files.empty()) {
        std::cerr << "No files to load" << std::endl;
        return false;
    }
    
    // Save z-height data before cleanup (for 3D mode)
    auto savedZHeights = collection.zHeights;
    int savedZIndex = collection.currentZIndex;
    auto savedZAllFilePaths = collection.zAllFilePaths;
    
    collection.cleanup();
    collection.currentFolder = folder;
    collection.allFilePaths = allFilePaths;
    
    // Restore z-height data after cleanup (for 3D mode)
    collection.zHeights = savedZHeights;
    collection.currentZIndex = savedZIndex;
    collection.zAllFilePaths = savedZAllFilePaths;
    
    std::cout << "\nFolder: " << folder << std::endl;
    std::cout << "Loading " << files.size() << " images with " << numThreads << " threads..." << std::endl;
    
    // Prepare frames vector
    collection.frames.resize(files.size());
    
    std::mutex progressMutex;
    int loadedCount = 0;
    int firstWidth = 0, firstHeight = 0;
    std::mutex dimMutex;
    
    // Worker function
    auto loadWorker = [&](size_t startIdx, size_t endIdx) {
        for (size_t i = startIdx; i < endIdx; i++) {
            // Check for interrupt
            if (g_interrupted.load()) {
                return;
            }
            
            int w, h;
            unsigned char* data = LoadAndShrinkImage(files[i], shrinkFactor, w, h, rgbOutput, flipVertical);
            
            if (data) {
                // Store first dimensions (thread-safe)
                {
                    std::lock_guard<std::mutex> lock(dimMutex);
                    if (firstWidth == 0) {
                        firstWidth = w;
                        firstHeight = h;
                    }
                }
                
                // Extract just the filename from path
                size_t lastSlash = files[i].find_last_of("/\\");
                std::string filename = (lastSlash != std::string::npos) ? 
                    files[i].substr(lastSlash + 1) : files[i];
                
                collection.frames[i].filename = filename;
                collection.frames[i].index = ExtractIndex(filename);
                collection.frames[i].data = data;
            } else {
                collection.frames[i].data = nullptr;
            }
            
            // Update progress
            {
                std::lock_guard<std::mutex> lock(progressMutex);
                loadedCount++;
                std::cout << "\rLoading: " << loadedCount << "/" << files.size() << std::flush;
                
                if (progressCallback) {
                    progressCallback(loadedCount, (int)files.size());
                }
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
    
    // Wait for all threads
    for (auto& thread : threads) {
        thread.join();
    }
    std::cout << std::endl;
    
    // Check if interrupted
    if (g_interrupted.load()) {
        std::cout << "\nLoading interrupted by user (Ctrl+C)" << std::endl;
        collection.cleanup();
        return false;
    }
    
    // Remove failed loads
    collection.frames.erase(
        std::remove_if(collection.frames.begin(), collection.frames.end(),
            [](const ImageFrame& f) { return f.data == nullptr; }),
        collection.frames.end()
    );
    
    if (collection.frames.empty()) {
        std::cerr << "No images could be loaded" << std::endl;
        return false;
    }
    
    // Sort by index
    std::sort(collection.frames.begin(), collection.frames.end(),
        [](const ImageFrame& a, const ImageFrame& b) { return a.index < b.index; });
    
    collection.imageWidth = firstWidth;
    collection.imageHeight = firstHeight;
    collection.originalImageWidth = firstWidth * shrinkFactor;
    collection.originalImageHeight = firstHeight * shrinkFactor;
    collection.currentFrame = 0;
    
    // Print memory stats
    size_t bytesPerImage = (size_t)firstWidth * firstHeight * 3;
    size_t totalBytes = bytesPerImage * collection.frames.size();
    
    std::cout << "\nMemory usage:" << std::endl;
    std::cout << "  Shrink factor: " << shrinkFactor << std::endl;
    std::cout << "  Preview: " << firstWidth << " x " << firstHeight << std::endl;
    std::cout << "  Original: " << collection.originalImageWidth << " x " << collection.originalImageHeight << std::endl;
    
    if (totalBytes < 1024 * 1024) {
        std::cout << "  Total RAM: " << (totalBytes / 1024.0) << " KB" << std::endl;
    } else if (totalBytes < 1024 * 1024 * 1024) {
        std::cout << "  Total RAM: " << (totalBytes / (1024.0 * 1024.0)) << " MB" << std::endl;
    } else {
        std::cout << "  Total RAM: " << (totalBytes / (1024.0 * 1024.0 * 1024.0)) << " GB" << std::endl;
    }
    
    std::cout << "\nLoaded " << collection.frames.size() << " images for preview" << std::endl;
    std::cout << "Export will use all " << collection.allFilePaths.size() << " files at full resolution" << std::endl;
    
    return true;
}
