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
                         QSize targetSize,
                         bool fullQuality,
                         CancelToken cancel)
    : m_index(index),
      m_path(path),
      m_targetSize(targetSize),
      m_fullQuality(fullQuality),
      m_cancel(std::move(cancel))
{
    // The pool must not delete us: we self-delete via finished() -> deleteLater
    // on the GUI thread, after the queued loaded() signal has been delivered.
    setAutoDelete(false);
}

void ImageLoader::run()
{
    // Decode into locals and emit exactly once at the end. Cancellation is
    // checked before each stage — especially before the expensive RAW
    // demosaic — and again before emitting, so a decode the GUI thread has
    // already abandoned neither burns CPU/memory nor pollutes the cache.
    QImage image;
    bool   emitFull   = false;
    bool   haveResult = false;

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

    const QString ext  = QFileInfo(m_path).suffix();
    const bool isRaw   = isRawExtension(ext);
    // Whether the image we deliver is final display quality. Non-RAW always is;
    // RAW only when a full demosaic was requested (fast previews get upgraded).
    const bool achievedFull = m_fullQuality || !isRaw;

    // 1) QImageReader (supports scaling and EXIF transforms). Skip it for a
    //    full-quality RAW request so we always take the LibRaw demosaic path
    //    rather than a plugin-provided embedded preview (keeps the
    //    no-quality-loss guarantee plugin-independent).
    if (!haveResult && !cancelled() && !(m_fullQuality && isRaw)) {
        QImageReader reader(m_path);
        reader.setAutoTransform(true); // honor EXIF orientation, etc.
        if (m_targetSize.isValid() && m_targetSize.width() > 0 && m_targetSize.height() > 0) {
            reader.setScaledSize(m_targetSize);
        }
        QImage tmp;
        if (reader.read(&tmp) && !tmp.isNull()) {
            image = tmp;
            emitFull = achievedFull;
            haveResult = true;
        }
    }

    // 2) Direct QImage::load() as a last quick attempt for non-RAW formats.
    if (!haveResult && !cancelled() && !isRaw) {
        QImage fallback(m_path);
        if (!fallback.isNull()) {
            if (m_targetSize.isValid() && m_targetSize.width() > 0 && m_targetSize.height() > 0) {
                fallback = fallback.scaled(m_targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
            image = fallback;
            emitFull = achievedFull;
            haveResult = true;
        }
    }

#ifdef HAVE_LIBRAW
    // 3) RAW via LibRaw. The viewed image is demosaiced at full resolution for
    //    maximum quality; the prefetch ring and thumbnails use the fast embedded
    //    preview. The cancellation checks here are what stop a folder full of
    //    RAWs from running a pile of heavyweight demosaics the user has already
    //    scrolled past.
    if (!haveResult && !cancelled() && isRaw) {
        QImage rawImage;
        bool rawLoaded = false; // avoid shadowing the 'loaded' signal

        if (m_fullQuality) {
            rawLoaded = RawLoader::loadDemosaiced(m_path, rawImage, /*halfSize=*/false);
            if (!rawLoaded && !cancelled())
                rawLoaded = RawLoader::loadEmbeddedPreview(m_path, rawImage);
        } else {
            if (RawLoader::loadEmbeddedPreview(m_path, rawImage))
                rawLoaded = true;
            else if (!cancelled())
                rawLoaded = RawLoader::loadDemosaiced(m_path, rawImage, /*halfSize=*/true);
        }

        if (rawLoaded && !rawImage.isNull()) {
            if (m_targetSize.isValid() && m_targetSize.width() > 0 && m_targetSize.height() > 0) {
                rawImage = rawImage.scaled(m_targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
            image = rawImage;
            emitFull = m_fullQuality;
            haveResult = true;
        }
    }
#else
    Q_UNUSED(isRaw);
#endif

    // 4) Placeholder on failure so callers can still show something.
    if (!haveResult && !cancelled()) {
        QImage placeholder(100, 100, QImage::Format_RGB32);
        placeholder.fill(QColor("lightgray"));
        image = placeholder;
        emitFull = achievedFull;
        haveResult = true;
    }

    // Deliver only if still wanted; never hand back a result for an abandoned
    // decode (it would re-populate a cache the GUI thread just cleared).
    if (haveResult && !cancelled()) {
        emit loaded(m_index, m_path, image, emitFull);
    }
    emit finished();
}
