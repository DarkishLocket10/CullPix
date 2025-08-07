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

class QLabel;
class QPushButton;
class QStatusBar;
class ImageLoader;

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
    void onImagePreloaded(int index, const QImage &image);

private:
    void loadSourceDirectory(const QString &directory);
    void displayCurrentImage();
    void preloadNext();
    void performMove(const QString &action);
    static bool naturalLess(const QFileInfo &a, const QFileInfo &b);

    // Data
    std::vector<QFileInfo> m_images;
    int m_currentIndex = -1;
    QHash<int, QImage> m_preloaded;
    std::deque<MoveAction> m_undoStack;
    static constexpr int MAX_UNDO = 20;

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
    ImageLoader *m_loader = nullptr;
};