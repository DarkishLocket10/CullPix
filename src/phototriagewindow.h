// phototriagewindow.h
//
// Declares the PhotoTriageWindow class which provides the main
// application window for culling images.

#pragma once

#include <QMainWindow>
#include <QImage>
#include <QHash>
#include <QFileInfo>
#include <vector>
#include <deque>
#include <QSet>

#include "imageloader.h"   // ImageLoader + CancelToken

class QLabel;
class QPushButton;
class QStatusBar;
class ImageView;
class QListWidget;
class QAction;
class QThreadPool;
class QTimer;

// Forward declarations for asynchronous file worker
struct FileTask;
class FileWorker;

// A decoded image plus whether it is final display quality. For RAW, a fast
// embedded preview has full == false and gets upgraded to a full demosaic when
// viewed; non-RAW decodes are always full. Keeping the flag with the pixels
// means cache eviction can never desync a parallel "is full" set.
struct CachedImage
{
    QImage image;
    bool   full = false;
};

// Record of a move operation for undo purposes
struct MoveAction
{
    QString originalPath;
    QString destinationPath;
    int index;
    // Decoded pixels retained so undo can restore the photo instantly without
    // a synchronous re-decode. May be null (e.g. for older entries, see
    // UNDO_IMAGE_RETAIN) in which case undo loads it asynchronously.
    QImage image;
    bool   imageFull = false;   // was `image` final display quality?
};

class PhotoTriageWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit PhotoTriageWindow(QWidget *parent = nullptr);
    ~PhotoTriageWindow() override;

protected:

    void resizeEvent(QResizeEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void chooseSourceFolder();
    void handleMoveKeep();
    void handleMoveReject();
    void undoLastAction();
    void onImagePreloaded(int index, const QString &path, const QImage &image, bool fullQuality);

    // Debounced upgrade of the *current* image to full display quality. Armed by
    // displayCurrentImage() and only fired once the user settles, so scrubbing
    // through a folder never spawns a pile of full-resolution RAW demosaics.
    void loadFullQualityForCurrent();

    // A queued async file move could not be completed (e.g. write-protected card).
    void onMoveFailed(const QString &source, const QString &destination);

    // Navigate to the next and previous images without making a keep/reject decision.
    void goToNextImage();
    void goToPreviousImage();

    // Handle selection changes in the file browser list.
    void onFileListSelectionChanged(int row);

private:
    int indexFromPath(const QString &path) const;

    void loadSourceDirectory(const QString &directory);
    void displayCurrentImage();
    void ensurePreloadWindow();
    void performMove(const QString &action);
    static bool naturalLess(const QFileInfo &a, const QFileInfo &b);

    // Launch a pooled, cancellable image decode (preview or full quality) and
    // record its cancellation token so it can be abandoned later.
    void startImageLoad(int index, const QString &key, bool fullQuality);
    // Start a preview prefetch for index `i` unless it is already cached/loading.
    void maybeStartPreview(int i);
    // Launch a pooled, cancellable thumbnail decode for the side list.
    void startThumbLoad(int index, const QString &path);
    // Trip every in-flight load's cancellation token and reset all load state.
    // Used when switching folders and on shutdown.
    void cancelAllLoads();

    QPushButton* m_openButton = nullptr;
    QAction* m_openAct = nullptr; // menu action
    QString m_lastDir; // remember last directory

    // Populate the side file browser with the current set of images. This helper
    // clears m_fileListWidget and inserts an entry for each element in
    // m_images.  It should be called whenever the contents of m_images change
    // substantially, such as after loading a directory, moving files, or
    // undoing an action.
    void populateFileList();

    // Data
    std::vector<QFileInfo> m_images;
    int m_currentIndex = -1;
    // Cache of preloaded images keyed by the absolute file path (with a
    // per-entry "is full quality" flag). Keying by path keeps the cache valid
    // even when indices shift after removing items.
    QHash<QString, CachedImage> m_preloaded;
    std::deque<MoveAction> m_undoStack;
    static constexpr int MAX_UNDO = 20;
    // Retain decoded pixels for only the most recent moves so undo is instant
    // without holding many full-resolution frames in memory. The common "oops"
    // undo only needs the last one; deeper undos load asynchronously.
    static constexpr int UNDO_IMAGE_RETAIN = 3;

    // Absolute paths with a prefetch (fast / preview-quality) load in flight.
    // Tracked by path (not index) so the guard stays valid across the row
    // shifts caused by keep/reject/undo.
    QSet<QString> m_loading;
    // Absolute paths with a full-quality load in flight for the *current*
    // image (full RAW demosaic / full decode). Separate from m_loading so a
    // pending fast preview never blocks the full-quality upgrade.
    QSet<QString> m_fullRequested;

    // Bounded thread pools that cap how many decodes run at once — the fix for
    // the unbounded full-resolution RAW demosaics that used to exhaust memory.
    // Heavy preview/full decodes run on m_decodePool; light side-list
    // thumbnails on m_thumbPool so they never starve the visible image.
    QThreadPool *m_decodePool = nullptr;
    QThreadPool *m_thumbPool  = nullptr;

    // Cancellation tokens for in-flight preview prefetches, keyed by path, so a
    // load that drifts out of the preload window (or whose folder changed) can
    // be abandoned. The single full-quality decode is tracked separately and
    // superseded whenever a new current image needs one.
    QHash<QString, CancelToken> m_imgTokens;
    QHash<QString, CancelToken> m_thumbTokens;
    CancelToken                 m_fullToken;

    // Coalesces full-quality decode requests during rapid navigation.
    QTimer *m_fullQualityTimer = nullptr;

    static constexpr int PRELOAD_DEPTH = 10;

    // Number of images behind the current index to keep preloaded in the
    // cache. Keeping a small window of previous images allows the user to
    // navigate backwards with minimal delay. This complements the forward
    // preload depth and helps deliver a fluid browsing experience.
    // Preload a generous number of previous images to enable lightning‑fast
    // backward navigation. Increasing this depth ensures that recently
    // viewed items remain in memory when the user presses the left arrow key.
    // A value of 5 strikes a balance between memory consumption and
    // performance.
    static constexpr int PRELOAD_BACK_DEPTH = 5;

    // Background worker for file operations
    FileWorker *m_fileWorker = nullptr;

    // Directories
    QString m_sourceDir;
    QString m_keepDir;
    QString m_discardDir;

    // UI elements
    ImageView *m_imageView;
    QStatusBar *m_statusBar;
    QPushButton *m_keepButton;
    QPushButton *m_rejectButton;
    QPushButton *m_undoButton;

    // Side panel for browsing available images. This list displays
    // thumbnails and filenames for all images in the current
    // directory and allows the user to jump directly to any photo.
    QListWidget *m_fileListWidget;

    // Thumbnail cache keyed by absolute file path. Each entry stores a
    // QPixmap that represents a small preview. Caching prevents
    // repeatedly decoding the same image when it appears in the file list.
    QHash<QString, QPixmap> m_thumbnailCache;

    // Set of file paths currently being loaded asynchronously for
    // thumbnails. Using the file path rather than the list index makes
    // the tracking robust when rows shift due to keep/discard/undo
    // operations. This prevents launching duplicate loaders for the same
    // image and provides a simple way to limit the number of concurrent
    // loads.
    QSet<QString> m_thumbLoadingPaths;

    // Kick off asynchronous thumbnail loading for any images that lack cached
    // thumbnails. Concurrency is bounded by m_thumbPool, so this can safely
    // enqueue every missing thumbnail at once.
    void startThumbnailLoaders();

    // Slot to receive loaded thumbnails. Updates the cache and the
    // corresponding list item's icon.  Connected to ImageLoader::loaded for
    // thumbnail loaders. Removes the file path from
    // m_thumbLoadingPaths and triggers another queued load if any are
    // pending.
    void onThumbnailLoaded(int index, const QString &path, const QImage &image);
    // Loader for current image is handled asynchronously via ImageLoader instances
};
