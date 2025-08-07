// imageloader.cpp

#include "imageloader.h"
#include <QImage>
#include <QColor>

ImageLoader::ImageLoader(int index, const QString &path, QObject *parent)
    : QThread(parent), m_index(index), m_path(path)
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
    // Attempt to load the image from disk.  QImage is reentrant
    // and therefore safe to construct on a worker thread.
    QImage image(m_path);
    if (image.isNull()) {
        // Provide a placeholder image to indicate failure
        QImage placeholder(100, 100, QImage::Format_RGB32);
        placeholder.fill(QColor("lightgray"));
        emit loaded(m_index, placeholder);
    } else {
        emit loaded(m_index, image);
    }
}