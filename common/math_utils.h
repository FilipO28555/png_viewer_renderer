// Math utilities for PNG Image Viewer
// Platform-independent zoom/pan calculations

#ifndef MATH_UTILS_H
#define MATH_UTILS_H

#include "frame_types.h"
#include <algorithm>

// Calculate the base scale to fit image in window
inline double GetFitScale(int windowWidth, int windowHeight, int imageWidth, int imageHeight) {
    if (imageWidth == 0 || imageHeight == 0) return 1.0;
    double scaleX = (double)windowWidth / imageWidth;
    double scaleY = (double)windowHeight / imageHeight;
    return std::min(scaleX, scaleY);
}

// Clamp pan values to keep image visible
inline void ClampPan(ViewState& view, const AppSettings& settings, int imageWidth, int imageHeight) {
    double fitScale = GetFitScale(settings.windowWidth, settings.windowHeight, imageWidth, imageHeight);
    double currentScale = fitScale * view.zoomLevel;
    
    // Calculate how much of the image is visible at current zoom
    double visibleWidth = settings.windowWidth / currentScale;
    double visibleHeight = settings.windowHeight / currentScale;
    
    // Calculate max pan (half the difference between image size and visible area)
    double maxPanX = std::max(0.0, (imageWidth - visibleWidth) / 2.0);
    double maxPanY = std::max(0.0, (imageHeight - visibleHeight) / 2.0);
    
    view.panX = std::clamp(view.panX, -maxPanX, maxPanX);
    view.panY = std::clamp(view.panY, -maxPanY, maxPanY);
}

// Apply zoom centered on a specific point
inline void ApplyZoom(ViewState& view, const AppSettings& settings, 
                      int imageWidth, int imageHeight,
                      int mouseX, int mouseY, double zoomFactor) {
    double fitScale = GetFitScale(settings.windowWidth, settings.windowHeight, imageWidth, imageHeight);
    double oldScale = fitScale * view.zoomLevel;
    
    // Calculate mouse position in image coordinates before zoom
    double mouseImgX = (mouseX - settings.windowWidth / 2.0) / oldScale + imageWidth / 2.0 + view.panX;
    double mouseImgY = (mouseY - settings.windowHeight / 2.0) / oldScale + imageHeight / 2.0 + view.panY;
    
    // Apply zoom with limits
    view.zoomLevel *= zoomFactor;
    view.zoomLevel = std::clamp(view.zoomLevel, settings.minZoom, settings.maxZoom);
    
    double newScale = fitScale * view.zoomLevel;
    
    // Adjust pan so mouse stays over the same image point
    view.panX = mouseImgX - imageWidth / 2.0 - (mouseX - settings.windowWidth / 2.0) / newScale;
    view.panY = mouseImgY - imageHeight / 2.0 - (mouseY - settings.windowHeight / 2.0) / newScale;
    
    ClampPan(view, settings, imageWidth, imageHeight);
}

// Apply pan from mouse drag
inline void ApplyPan(ViewState& view, const AppSettings& settings,
                     int imageWidth, int imageHeight,
                     int mouseX, int mouseY) {
    double fitScale = GetFitScale(settings.windowWidth, settings.windowHeight, imageWidth, imageHeight);
    double currentScale = fitScale * view.zoomLevel;
    
    // Convert pixel movement to image coordinate movement
    double dx = (view.lastMouseX - mouseX) / currentScale;
    double dy = (view.lastMouseY - mouseY) / currentScale;
    
    view.panX += dx;
    view.panY += dy;
    
    ClampPan(view, settings, imageWidth, imageHeight);
    
    view.lastMouseX = mouseX;
    view.lastMouseY = mouseY;
}

// Calculate rendering parameters for current view
struct RenderParams {
    double currentScale;
    double centerX, centerY;
    double visibleWidth, visibleHeight;
    int srcX, srcY, srcW, srcH;
    int dstX, dstY, dstW, dstH;
};

inline RenderParams CalculateRenderParams(const ViewState& view, const AppSettings& settings,
                                          int imageWidth, int imageHeight) {
    RenderParams params;
    
    double fitScale = GetFitScale(settings.windowWidth, settings.windowHeight, imageWidth, imageHeight);
    params.currentScale = fitScale * view.zoomLevel;
    
    params.visibleWidth = settings.windowWidth / params.currentScale;
    params.visibleHeight = settings.windowHeight / params.currentScale;
    
    params.centerX = imageWidth / 2.0 + view.panX;
    params.centerY = imageHeight / 2.0 + view.panY;
    
    // Source rectangle (what part of the image to show)
    params.srcX = (int)(params.centerX - params.visibleWidth / 2.0);
    params.srcY = (int)(params.centerY - params.visibleHeight / 2.0);
    params.srcW = (int)params.visibleWidth;
    params.srcH = (int)params.visibleHeight;
    
    // Clamp source to image bounds
    if (params.srcX < 0) params.srcX = 0;
    if (params.srcY < 0) params.srcY = 0;
    if (params.srcX + params.srcW > imageWidth) params.srcW = imageWidth - params.srcX;
    if (params.srcY + params.srcH > imageHeight) params.srcH = imageHeight - params.srcY;
    
    // Destination rectangle
    params.dstX = (int)((params.srcX - (params.centerX - params.visibleWidth / 2.0)) * params.currentScale);
    params.dstY = (int)((params.srcY - (params.centerY - params.visibleHeight / 2.0)) * params.currentScale);
    params.dstW = (int)(params.srcW * params.currentScale);
    params.dstH = (int)(params.srcH * params.currentScale);
    
    return params;
}

#endif // MATH_UTILS_H
