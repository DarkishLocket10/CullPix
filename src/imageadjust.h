// imageadjust.h
//
// Provides utility functions for applying basic image adjustments such as
// exposure, contrast and highlight recovery.  These operations convert
// 8‑bit sRGB images into a linear floating point representation, apply
// the requested adjustments, then convert back to 8‑bit sRGB.  The
// routines here are designed to be stateless and can be called from
// both the editor preview and the final full‑resolution rendering.

#pragma once

#include <QImage>

namespace ImageAdjust {

// Convert an sRGB component (0–1) to linear light.  Uses the ITU
// BT.709 transfer function.  Values outside [0,1] are clamped.
inline float srgbToLinear(float c)
{
    if (c <= 0.04045f)
        return c / 12.92f;
    return std::pow((c + 0.055f) / 1.055f, 2.4f);
}

// Convert a linear light component back to sRGB (0–1).
inline float linearToSrgb(float c)
{
    if (c <= 0.0031308f)
        return c * 12.92f;
    return 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

// Clamp a value between 0 and 1.
inline float clamp01(float v)
{
    return v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
}

// Apply highlight compression to a linear component.  The factor
// determines the strength of the compression.  A factor of 0
// performs no adjustment.  Larger values progressively compress
// intensities above a threshold (0.9) to recover detail in bright
// regions.  This simple knee curve applies the compression
// independently to each colour channel.
inline float compressHighlight(float v, float factor)
{
    if (factor <= 0.f)
        return v;
    const float threshold = 0.9f;
    if (v <= threshold)
        return v;
    // Normalise the region above the threshold into [0,1]
    float t = (v - threshold) / (1.f - threshold);
    // Apply an exponent based on the factor to compress the range.
    float exp = 1.f + factor * 4.f;
    float compressed = std::pow(t, exp);
    return threshold + (1.f - threshold) * compressed;
}

// Adjust a single linear component using exposure and contrast.  The
// exposure is expressed in EV stops: a value of 1 doubles the
// brightness while ‑1 halves it.  Contrast is a multiplicative
// factor applied around the mid‑tone (0.5 in linear space).  A
// contrast of 1 leaves the image unchanged; values above 1 increase
// contrast while values below 1 reduce it.
inline float adjustComponent(float v, float exposureEV, float contrast)
{
    // Apply exposure (multiply by 2^EV)
    v *= std::pow(2.f, exposureEV);
    // Apply contrast around the mid‑tone.  Mid‑tone in linear light is 0.5.
    const float mid = 0.5f;
    v = mid + contrast * (v - mid);
    return v;
}

// Core adjustment routine.  Given an input QImage (any format), apply
// exposure (in EV stops), contrast (relative factor) and highlight
// recovery (0–1) and return a new QImage in 8‑bit sRGB.  Pixels
// outside the [0,1] range are clamped.  Alpha, if present, is
// preserved.
inline QImage applyAdjustments(const QImage &src,
                              float exposureEV,
                              float contrastFactor,
                              float highlightFactor)
{
    if (src.isNull())
        return QImage();
    // Convert to a format with explicit RGB channels.  Use Format_ARGB32
    // so that alpha, if present, is carried through.
    QImage img = src.convertToFormat(QImage::Format_ARGB32);
    const int w = img.width();
    const int h = img.height();
    QImage out(w, h, QImage::Format_ARGB32);
    // Iterate over all pixels.  Use scanLine for efficiency.
    for (int y = 0; y < h; ++y) {
        const QRgb *inLine = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        QRgb *outLine = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            QRgb px = inLine[x];
            float r = srgbToLinear(qRed(px) / 255.f);
            float g = srgbToLinear(qGreen(px) / 255.f);
            float b = srgbToLinear(qBlue(px) / 255.f);
            // Apply exposure and contrast to each channel
            r = adjustComponent(r, exposureEV, contrastFactor);
            g = adjustComponent(g, exposureEV, contrastFactor);
            b = adjustComponent(b, exposureEV, contrastFactor);
            // Highlight compression
            r = compressHighlight(r, highlightFactor);
            g = compressHighlight(g, highlightFactor);
            b = compressHighlight(b, highlightFactor);
            // Clamp
            r = clamp01(r);
            g = clamp01(g);
            b = clamp01(b);
            // Convert back to sRGB
            int R = int(linearToSrgb(r) * 255.f + 0.5f);
            int G = int(linearToSrgb(g) * 255.f + 0.5f);
            int B = int(linearToSrgb(b) * 255.f + 0.5f);
            // Preserve alpha
            int A = qAlpha(px);
            outLine[x] = qRgba(R, G, B, A);
        }
    }
    return out;
}

} // namespace ImageAdjust
