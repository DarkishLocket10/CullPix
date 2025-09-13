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
class QPushButton;
class QStatusBar;
class ImageLoader;
class QListWidget;
class QAction;

// Forward declarations for asynchronous file worker
struct FileTask;
class FileWorker;

// Record of a move operation for undo purposes
struct MoveAction
{
    QString originalPath;
    QString destinationPath;
    int index;
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
    void onImagePreloaded(int index, const QString &path, const QImage &image);

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
    // Cache of preloaded images keyed by the absolute file path. This
    // allows the cache to remain valid even when indices shift after
    // removing items.
    QHash<QString, QImage> m_preloaded;
    std::deque<MoveAction> m_undoStack;
    static constexpr int MAX_UNDO = 20;

    // Preloading queue: indices of images currently loading ahead
    QSet<int> m_loading;
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
    QLabel *m_imageLabel;
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
