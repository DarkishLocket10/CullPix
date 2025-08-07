// imageloader.h
//
// Defines the ImageLoader class used to asynchronously load
// QImage objects on a worker thread.  This helps to keep the
// user interface responsive when dealing with large image files.

#pragma once

#include <QImage>
#include <QThread>

// ImageLoader runs in its own QThread and emits a signal when
// loading has finished.  It does not convert to QPixmap here because
// pixmap creation must occur on the GUI thread on some platforms.
class ImageLoader : public QThread
{
    Q_OBJECT
public:
    ImageLoader(int index, const QString &path, QObject *parent = nullptr);
    ~ImageLoader() override;

signals:
    // Emitted when the image has been loaded.  The index identifies
    // which entry in the image list this corresponds to.
    void loaded(int index, const QImage &image);

protected:
    // QThread::run() executes in the worker thread.
    void run() override;

private:
    int m_index;
    QString m_path;
};