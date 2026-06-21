// imageloader.h
//
// Defines the ImageLoader class used to asynchronously decode a single image
// off the GUI thread.
//
// ImageLoader is a QRunnable (so many decodes share a *bounded* QThreadPool
// instead of spawning one OS thread per image) and a QObject (so it can deliver
// its result through a queued signal). Every loader carries a CancelToken that
// the GUI thread can trip to abandon a decode whose result is no longer wanted
// — when the user navigates away, switches folders, or quits — before the
// expensive work (notably a full-resolution RAW demosaic) is performed.

#pragma once

#include <QImage>
#include <QObject>
#include <QRunnable>
#include <QSize>
#include <QString>

#include <atomic>
#include <memory>

// A cancellation flag shared between the GUI thread (which sets it) and the
// worker (which polls it). A shared_ptr keeps it alive no matter which side
// finishes first; the atomic makes the cross-thread access well-defined.
using CancelToken = std::shared_ptr<std::atomic_bool>;
inline CancelToken makeCancelToken() { return std::make_shared<std::atomic_bool>(false); }

class ImageLoader : public QObject, public QRunnable
{
    Q_OBJECT
public:
    // fullQuality only affects RAW files: when true the worker does a full,
    // full-resolution demosaic (maximum quality, slower, memory heavy); when
    // false it uses the fast embedded preview. Non-RAW formats decode fully
    // either way. `cancel` lets the caller abandon the decode early.
    ImageLoader(int index,
                const QString &path,
                QSize targetSize,
                bool fullQuality,
                CancelToken cancel);

signals:
    // Emitted (unless cancelled) with the decoded image. `path` keys the cache;
    // `fullQuality` reports whether the delivered image is final display quality
    // (a JPEG/PNG decode or a full RAW demosaic) versus a fast RAW preview to be
    // upgraded later.
    void loaded(int index, const QString &path, const QImage &image, bool fullQuality);

    // Emitted exactly once when run() returns (cancelled or not), so the owner
    // can deleteLater() the loader on the GUI thread.
    void finished();

protected:
    // QRunnable entry point; executes on a QThreadPool worker thread.
    void run() override;

private:
    bool cancelled() const
    {
        return m_cancel && m_cancel->load(std::memory_order_relaxed);
    }

    int         m_index;
    QString     m_path;
    QSize       m_targetSize;   // optional decode target (e.g. thumbnail size)
    bool        m_fullQuality;  // RAW: full demosaic vs fast embedded preview
    CancelToken m_cancel;
};
