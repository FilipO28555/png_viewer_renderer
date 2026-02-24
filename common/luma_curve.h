// luma_curve.h
// Platform-independent luma curve model and spline math.
//
// A LumaCurve holds an ordered list of control points in normalised [0,1]x[0,1]
// space and produces a lookup table (0-255 -> 0-255) via monotone cubic
// (Fritsch-Carlson) spline interpolation.  The spline is monotone so the
// output stays within [0,1] and never overshoots.
//
// Usage:
//   LumaCurve curve;
//   curve.buildLUT();                       // call after any point change
//   unsigned char out = curve.lut[in];      // apply to a pixel value

#ifndef LUMA_CURVE_H
#define LUMA_CURVE_H

#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdint>

// ---------------------------------------------------------------------------
// Control point — x and y are normalised to [0, 1]
// ---------------------------------------------------------------------------
struct CurvePoint {
    double x, y;
};

// ---------------------------------------------------------------------------
// LumaCurve
// ---------------------------------------------------------------------------
struct LumaCurve {
    std::vector<CurvePoint> points;   // always kept sorted by x
    uint8_t lut[256];                 // final lookup table, rebuilt via buildLUT()
    bool enabled = false;             // whether the curve is applied to the display

    LumaCurve() {
        reset();
    }

    // Reset to identity (straight diagonal)
    void reset() {
        points = { {0.0, 0.0}, {1.0, 1.0} };
        buildLUT();
    }

    // Keep points sorted by x; call after any add/move
    void sortPoints() {
        std::sort(points.begin(), points.end(),
                  [](const CurvePoint& a, const CurvePoint& b){ return a.x < b.x; });
    }

    // Add a point (sorted insert).  Returns the index of the new point.
    int addPoint(double x, double y) {
        x = std::clamp(x, 0.0, 1.0);
        y = std::clamp(y, 0.0, 1.0);
        points.push_back({x, y});
        sortPoints();
        // find the index we just inserted
        for (int i = 0; i < (int)points.size(); ++i) {
            if (points[i].x == x && points[i].y == y) return i;
        }
        return 0;
    }

    // Move point at index; returns false if the move would cross a neighbour
    // (caller can clamp x to the allowed range before calling).
    void movePoint(int idx, double x, double y) {
        if (idx < 0 || idx >= (int)points.size()) return;
        // The two anchor points (first and last) can only move vertically
        if (idx == 0)                          x = 0.0;
        if (idx == (int)points.size() - 1)    x = 1.0;
        // Clamp x so it stays between neighbours
        double xMin = (idx > 0)                        ? points[idx-1].x + 1e-4 : 0.0;
        double xMax = (idx < (int)points.size() - 1)  ? points[idx+1].x - 1e-4 : 1.0;
        points[idx].x = std::clamp(x, xMin, xMax);
        points[idx].y = std::clamp(y, 0.0, 1.0);
    }

    // Remove point at index.  The two end-points (index 0 and last) cannot be removed.
    void removePoint(int idx) {
        if (idx <= 0 || idx >= (int)points.size() - 1) return;
        points.erase(points.begin() + idx);
    }

    // Find the nearest point within a normalised radius.  Returns -1 if none.
    int findNearest(double x, double y, double radiusNorm) const {
        int best = -1;
        double bestDist = radiusNorm * radiusNorm;
        for (int i = 0; i < (int)points.size(); ++i) {
            double dx = points[i].x - x;
            double dy = points[i].y - y;
            double d2 = dx*dx + dy*dy;
            if (d2 < bestDist) { bestDist = d2; best = i; }
        }
        return best;
    }

    // ---------------------------------------------------------------------------
    // Build the 256-entry lookup table using Fritsch-Carlson monotone cubic spline.
    // This guarantees no overshoot and a strictly non-decreasing mapping (if the
    // control points are monotone in y, which the editor enforces).
    // ---------------------------------------------------------------------------
    void buildLUT() {
        const int N = (int)points.size();

        // Edge case: only one point — identity
        if (N == 0) {
            for (int i = 0; i < 256; ++i) lut[i] = (uint8_t)i;
            return;
        }
        if (N == 1) {
            uint8_t v = (uint8_t)std::round(points[0].y * 255.0);
            for (int i = 0; i < 256; ++i) lut[i] = v;
            return;
        }

        // ----- Step 1: compute slopes at each knot (Fritsch-Carlson) ------
        std::vector<double> d(N-1), m(N, 0.0);

        for (int i = 0; i < N-1; ++i) {
            double dx = points[i+1].x - points[i].x;
            if (dx < 1e-10) dx = 1e-10;
            d[i] = (points[i+1].y - points[i].y) / dx;
        }

        // Initial tangents: average of adjacent secant slopes
        m[0] = d[0];
        for (int i = 1; i < N-1; ++i) m[i] = (d[i-1] + d[i]) * 0.5;
        m[N-1] = d[N-2];

        // Monotonicity fix (Fritsch-Carlson step)
        for (int i = 0; i < N-1; ++i) {
            if (std::abs(d[i]) < 1e-10) {
                m[i] = m[i+1] = 0.0;
                continue;
            }
            double alpha = m[i]   / d[i];
            double beta  = m[i+1] / d[i];
            double h     = std::hypot(alpha, beta);
            if (h > 3.0) {
                m[i]   = 3.0 * d[i] * alpha / h;
                m[i+1] = 3.0 * d[i] * beta  / h;
            }
        }

        // ----- Step 2: evaluate spline for each LUT entry ------------------
        for (int lut_i = 0; lut_i < 256; ++lut_i) {
            double t = lut_i / 255.0;

            // Find the segment containing t
            int seg = N - 2;  // default: last segment
            for (int i = 0; i < N-1; ++i) {
                if (t <= points[i+1].x) { seg = i; break; }
            }

            double x0 = points[seg].x,   x1 = points[seg+1].x;
            double y0 = points[seg].y,   y1 = points[seg+1].y;
            double h  = x1 - x0;
            if (h < 1e-10) h = 1e-10;

            double u = (t - x0) / h;                  // local parameter in [0,1]

            // Hermite basis
            double u2 = u * u, u3 = u2 * u;
            double h00 =  2*u3 - 3*u2 + 1;
            double h10 =    u3 - 2*u2 + u;
            double h01 = -2*u3 + 3*u2;
            double h11 =    u3 -   u2;

            double val = h00*y0 + h10*h*m[seg] + h01*y1 + h11*h*m[seg+1];
            val = std::clamp(val, 0.0, 1.0);
            lut[lut_i] = (uint8_t)std::round(val * 255.0);
        }
    }
};

#endif // LUMA_CURVE_H
