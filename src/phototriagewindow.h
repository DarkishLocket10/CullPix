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
#include <QQueue>

class QLabel;
class QProgressBar;
class QPushButton;
class QStatusBar;
class ImageLoader;
class ImageView;
class QListWidget;
class QAction;
class QDockWidget;
class QToolBar;
class QSlider;
class QMenu;

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
    QString kind;   // "keep" or "discard" — lets undo adjust the right counter
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
    // Re-dock the timeline (instead of hiding) when its floating window is closed.
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void chooseSourceFolder();
    void handleMoveKeep();
    void handleMoveReject();
    void undoLastAction();
    void onImagePreloaded(int index, const QString &path, const QImage &image, bool fullQuality);

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
    void preloadNext();
    void performMove(const QString &action);
    // Refresh the status-bar counts label + progress bar from the kept/rejected
    // counters and the remaining image count.
    void refreshProgress();
    // Lay the timeline list out as a single-row filmstrip (docked top/bottom)
    // or a wrapping grid (left/right/floating), and sync the position radio.
    void updateTimelineLayout();
    // Apply accent-color or monochrome styling to the Keep/Reject/Undo buttons.
    void applyButtonStyle();
    static bool naturalLess(const QFileInfo &a, const QFileInfo &b);

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

    // Status-bar triage progress.
    QLabel *m_countsLabel = nullptr;
    QProgressBar *m_progressBar = nullptr;
    int m_keptCount = 0;      // images sent to keep/ this session
    int m_rejectedCount = 0;  // images sent to discard/ this session
    int m_totalCount = 0;     // image count when the folder was opened

    // Side panel ("timeline") for browsing available images, hosted in a dock
    // widget so it can be moved to any edge, floated, or hidden.
    QListWidget *m_fileListWidget;
    QDockWidget *m_timelineDock = nullptr;
    QToolBar *m_toolBar = nullptr;          // bottom button bar (always visible)
    QWidget *m_actionButtons = nullptr;     // Open/Keep/Reject/Undo group (hide-able)
    QPushButton *m_timelineButton = nullptr; // quick show/hide on the toolbar
    QSlider *m_thumbSlider = nullptr;        // thumbnail-size control in the dock
    QHash<int, QAction*> m_timelinePosActions; // dock area -> position radio
    bool m_showNames = false;   // show filenames under grid thumbnails (default off)
    bool m_monochrome = false;  // neutral (no accent color) Keep/Reject/Undo buttons

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

    // Queue of thumbnail indices awaiting loading. When thumbnails
    // are missing from the cache, their indices are enqueued here and
    // processed in a limited‑concurrency manner. This avoids
    // spawning one thread per thumbnail and dramatically improves
    // responsiveness when loading large folders.
    QQueue<int> m_thumbPending;

    // Maximum number of thumbnail loads to run concurrently. Keeping
    // this number small prevents CPU and I/O saturation while still
    // populating thumbnails quickly in the background.
    static constexpr int MAX_THUMB_CONCURRENCY = 3;
    // Thumbnails decode at this size so the zoomable grid stays crisp.
    static constexpr int THUMB_PX = 160;

    // Kick off asynchronous thumbnail loading for any images that lack
    // cached thumbnails. Populates m_thumbPending and starts up to
    // MAX_THUMB_CONCURRENCY loaders immediately.
    void startThumbnailLoaders();

    // Start the next queued thumbnail loader if fewer than
    // MAX_THUMB_CONCURRENCY loads are currently running.
    void startNextThumbnailLoader();

    // Slot to receive loaded thumbnails. Updates the cache and the
    // corresponding list item's icon.  Connected to ImageLoader::loaded for
    // thumbnail loaders. Removes the file path from
    // m_thumbLoadingPaths and triggers another queued load if any are
    // pending.
    void onThumbnailLoaded(int index, const QString &path, const QImage &image);
    // Loader for current image is handled asynchronously via ImageLoader instances
};
