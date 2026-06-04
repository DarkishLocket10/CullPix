// imageloader.h
//
// Defines the ImageLoader class used to asynchronously load
// QImage objects on a worker thread. This helps to keep the
// user interface responsive when dealing with large image files.

#pragma once

#include <QImage>
#include <QThread>
#include <QSize>
#include <QString>

// ImageLoader runs in its own QThread and emits a signal when
// loading has finished. It does not convert to QPixmap here because
// pixmap creation must occur on the GUI thread on some platforms.
class ImageLoader : public QThread
{
    Q_OBJECT
public:
    // fullQuality only affects RAW files: when true the worker does a full,
    // full-resolution demosaic (maximum quality, slower); when false it uses
    // the fast embedded preview. Non-RAW formats decode fully either way.
    ImageLoader(int index,
                const QString &path,
                QObject *parent = nullptr,
                QSize targetSize = QSize(),
                bool fullQuality = false);
    ~ImageLoader() override;

signals:
    // Emitted when the image has been loaded. The index identifies which entry
    // in the image list this corresponds to, and the associated file path is
    // provided to allow keying the cache by path instead of index. fullQuality
    // reports whether the delivered image is final display quality (a JPEG/PNG
    // decode, or a full RAW demosaic) versus a fast RAW preview to be upgraded.
    void loaded(int index, const QString &path, const QImage &image, bool fullQuality);

protected:
    // QThread::run() executes in the worker thread.
    void run() override;

private:
    int m_index;
    QString m_path;
    QSize m_targetSize;   // optional decode target (e.g. thumbnail size)
    bool m_fullQuality;   // RAW: full demosaic vs fast embedded preview
};
