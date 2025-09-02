// phototriagewindow.h
//
// Declares the PhotoTriageWindow class which provides the main
// application window for culling images.  Only the keep/reject
// functionality is implemented in this version.

#pragma once

#include <QMainWindow>
#include <QImage>
#include <QHash>
#include <QFileInfo>
#include <vector>
#include <deque>
#include <QSet>

class QLabel;
class QPushButton;
class QStatusBar;
class ImageLoader;

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

private:
    int indexFromPath(const QString &path) const;

    void loadSourceDirectory(const QString &directory);
    void displayCurrentImage();
    void ensurePreloadWindow();
    void preloadNext();
    void performMove(const QString &action);
    static bool naturalLess(const QFileInfo &a, const QFileInfo &b);

    // Data
    std::vector<QFileInfo> m_images;
    int m_currentIndex = -1;
    // Cache of preloaded images keyed by the absolute file path.  This
    // allows the cache to remain valid even when indices shift after
    // removing items.
    QHash<QString, QImage> m_preloaded;
    std::deque<MoveAction> m_undoStack;
    static constexpr int MAX_UNDO = 20;

    // Preloading queue: indices of images currently loading ahead
    QSet<int> m_loading;
    static constexpr int PRELOAD_DEPTH = 10;

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
    // Loader for current image is handled asynchronously via ImageLoader instances
    // created in ensurePreloadWindow().  A dedicated member is no longer needed.
};
