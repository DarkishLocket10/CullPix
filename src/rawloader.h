// rawloader.h
#pragma once
#include <QImage>
#include <QString>

namespace RawLoader {
    // Fast: use embedded preview (JPEG) if present.
    bool loadEmbeddedPreview(const QString& path, QImage& out);

    // Full demosaic to 8-bit sRGB (heavier but best quality).
    bool loadDemosaiced(const QString& path, QImage& out,
                        bool halfSize=true); // halfSize is faster.
}
