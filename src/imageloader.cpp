// imageloader.cpp

#include "imageloader.h"
#include <QImageReader>
#include <QColor>

ImageLoader::ImageLoader(int index,
                         const QString &path,
                         QObject *parent,
                         QSize targetSize)
    : QThread(parent),
    m_index(index),
    m_path(path),
    m_targetSize(targetSize)
{
    // nothing else
    }

ImageLoader::~ImageLoader()
{
    // Ensure the thread is stopped before destruction
    requestInterruption();
    quit();
    wait();
}

void ImageLoader::run()
{
    if (isInterruptionRequested())
        return;

    QImage image;
    QImageReader reader(m_path);
    reader.setAutoTransform(true); // honor EXIF orientation, etc.

    // If a valid target size was supplied, decode at that size (faster & less RAM)
    if (m_targetSize.isValid() && m_targetSize.width() > 0 && m_targetSize.height() > 0) {
        reader.setScaledSize(m_targetSize);
    }

    if (!isInterruptionRequested() && reader.read(&image) && !image.isNull()) {
        emit loaded(m_index, m_path, image);
        return;
    }

    // Fallback: try direct load if plugin couldn’t scale-read
    if (!isInterruptionRequested()) {
        QImage fallback(m_path);
        if (!fallback.isNull()) {
            emit loaded(m_index, m_path, fallback);
            return;
        }
    }

    // Failure: emit a simple placeholder
    if (!isInterruptionRequested()) {
        QImage placeholder(100, 100, QImage::Format_RGB32);
        placeholder.fill(QColor("lightgray"));
        emit loaded(m_index, m_path, placeholder);
    }
}
