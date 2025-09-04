// imageloader.cpp

#include "imageloader.h"
#include <QImageReader>
#include <QColor>
#include <QFileInfo>
#include <QSet>

// Optional raw support: Only include and use RawLoader when LibRaw is available.
#ifdef HAVE_LIBRAW
#include "rawloader.h"
#endif

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

    // Attempt to load the file in several ways. First try using
    // Qt's image reader (covers JPEG/PNG/etc.). If that fails and the
    // file has a RAW extension we fall back to LibRaw via RawLoader
    // (when available). If all attempts fail, emit a simple placeholder.
    QImage image;

    // Check for RAW file extension up front. Comparison uses lower-case
    // suffixes; any extension present in the set below will be considered
    // a RAW format handled by LibRaw.
    auto isRawExtension = [](const QString &ext) {
        static const QSet<QString> rawExts = {
            QStringLiteral("arw"), QStringLiteral("cr2"), QStringLiteral("cr3"),
            QStringLiteral("nef"), QStringLiteral("nrw"), QStringLiteral("raf"),
            QStringLiteral("rw2"), QStringLiteral("rwl"), QStringLiteral("orf"),
            QStringLiteral("pef"), QStringLiteral("srw"), QStringLiteral("dng"),
            QStringLiteral("raw")
        };
        return rawExts.contains(ext.toLower());
    };

    const QString ext = QFileInfo(m_path).suffix();
    const bool isRaw = isRawExtension(ext);

    // 1) Try QImageReader (supports scaling and EXIF transforms).
    {
        QImageReader reader(m_path);
        reader.setAutoTransform(true); // honor EXIF orientation, etc.

        if (m_targetSize.isValid() && m_targetSize.width() > 0 && m_targetSize.height() > 0) {
            reader.setScaledSize(m_targetSize);
        }

        if (!isInterruptionRequested() && reader.read(&image) && !image.isNull()) {
            emit this->loaded(m_index, m_path, image);
            return;
        }
    }

    // 2) Try direct QImage::load() as a last quick attempt for non-RAW formats.
    if (!isInterruptionRequested() && !isRaw) {
        QImage fallback(m_path);
        if (!fallback.isNull()) {
            if (m_targetSize.isValid() && m_targetSize.width() > 0 && m_targetSize.height() > 0) {
                fallback = fallback.scaled(m_targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
            emit this->loaded(m_index, m_path, fallback);
            return;
        }
    }

#ifdef HAVE_LIBRAW
    // 3) RAW fallback via LibRaw (fast embedded preview first, then half-size demosaic).
    if (!isInterruptionRequested()) {
        QImage rawImage;
        bool rawLoaded = false; // avoid shadowing the 'loaded' signal

        if (isRaw) {
            // Try embedded preview (usually a JPEG) – fast and great for thumbnails.
            if (RawLoader::loadEmbeddedPreview(m_path, rawImage)) {
                rawLoaded = true;
            } else {
                // Fallback: half-size demosaic for speed/memory.
                rawLoaded = RawLoader::loadDemosaiced(m_path, rawImage, /*halfSize=*/true);
            }
        }

        if (rawLoaded && !rawImage.isNull()) {
            if (m_targetSize.isValid() && m_targetSize.width() > 0 && m_targetSize.height() > 0) {
                rawImage = rawImage.scaled(m_targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
            emit this->loaded(m_index, m_path, rawImage);
            return;
        }
    }
#else
    Q_UNUSED(isRaw);
#endif

    // 4) Emit a simple placeholder on failure so callers can still show something.
    if (!isInterruptionRequested()) {
        QImage placeholder(100, 100, QImage::Format_RGB32);
        placeholder.fill(QColor("lightgray"));
        emit this->loaded(m_index, m_path, placeholder);
    }
}
