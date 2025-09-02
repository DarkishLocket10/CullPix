// rawloader.h
#pragma once
#include <QImage>
#include <QString>

class RawLoader {
public:
    // Try fast embedded preview first. If not present, optional half demosaic.
    static QImage loadPreviewOrHalf(const QString& path, bool allowHalfDemosaic=true);
    static bool isRawExtension(const QString& path);
};
