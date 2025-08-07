// phototriagewindow.cpp
//
// Implements the PhotoTriageWindow class.  This class provides
// the main user interface for browsing and culling images.  The
// implementation focuses on keep/reject behaviour and includes a
// basic undo stack.

#include "phototriagewindow.h"
#include "imageloader.h"
#include <algorithm>
#include <QThread>
#include <QLabel>
#include <QPushButton>
#include <QStatusBar>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QShortcut>
#include <QKeySequence>
#include <QToolBar>
#include <QMessageBox>
#include <QResizeEvent>
#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QApplication>
#include <QTimer>
#include <cctype>

PhotoTriageWindow::PhotoTriageWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Photo‑Triage"));
    resize(800, 600);

    // Set up central image display
    m_imageLabel = new QLabel(this);
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_imageLabel->setStyleSheet("background-color: #111; color: #eee;");
    setCentralWidget(m_imageLabel);

    // Status bar
    m_statusBar = statusBar();

    // Buttons
    m_keepButton = new QPushButton(tr("Keep (Z)"));
    m_rejectButton = new QPushButton(tr("Reject (X)"));
    m_undoButton = new QPushButton(tr("Undo (U)"));

    // Connect button clicks to handlers
    connect(m_keepButton, &QPushButton::clicked, this, &PhotoTriageWindow::handleMoveKeep);
    connect(m_rejectButton, &QPushButton::clicked, this, &PhotoTriageWindow::handleMoveReject);
    connect(m_undoButton, &QPushButton::clicked, this, &PhotoTriageWindow::undoLastAction);

    // Tool bar layout
    QWidget *toolbarWidget = new QWidget(this);
    QHBoxLayout *hbox = new QHBoxLayout(toolbarWidget);
    hbox->setContentsMargins(5, 5, 5, 5);
    hbox->setSpacing(10);
    hbox->addWidget(m_keepButton);
    hbox->addWidget(m_rejectButton);
    hbox->addWidget(m_undoButton);
    toolbarWidget->setLayout(hbox);
    QToolBar *tb = new QToolBar(this);
    tb->setMovable(false);
    tb->addWidget(toolbarWidget);
    addToolBar(Qt::BottomToolBarArea, tb);

    // Shortcuts
    new QShortcut(QKeySequence(QStringLiteral("Z")), this, SLOT(handleMoveKeep()));
    new QShortcut(QKeySequence(QStringLiteral("X")), this, SLOT(handleMoveReject()));
    new QShortcut(QKeySequence(QStringLiteral("U")), this, SLOT(undoLastAction()));
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+Z")), this, SLOT(undoLastAction()));
    new QShortcut(QKeySequence(QStringLiteral("O")), this, SLOT(chooseSourceFolder()));

    // Ask for source folder on startup after event loop starts
    QTimer::singleShot(0, this, &PhotoTriageWindow::chooseSourceFolder);
}

PhotoTriageWindow::~PhotoTriageWindow()
{
    // Ensure the loader thread stops gracefully
    if (m_loader && m_loader->isRunning()) {
        m_loader->requestInterruption();
        m_loader->quit();
        m_loader->wait();
    }
    delete m_loader;
}

void PhotoTriageWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    displayCurrentImage();
}

void PhotoTriageWindow::closeEvent(QCloseEvent *event)
{
    // Make sure worker thread stops
    if (m_loader && m_loader->isRunning()) {
        m_loader->requestInterruption();
        m_loader->quit();
        m_loader->wait();
    }
    QMainWindow::closeEvent(event);
}

void PhotoTriageWindow::chooseSourceFolder()
{
    // Use QFileDialog to choose a directory.  Use the static method
    // so that the window remains modal and the UI code stays simple.
    QFileDialog dlg(this, tr("Select Source Folder"));
    dlg.setFileMode(QFileDialog::Directory);
    dlg.setOption(QFileDialog::ShowDirsOnly, true);
    if (dlg.exec() == QDialog::Accepted && !dlg.selectedFiles().isEmpty()) {
        loadSourceDirectory(dlg.selectedFiles().first());
    }
}

bool PhotoTriageWindow::naturalLess(const QFileInfo &a, const QFileInfo &b)
{
    // Compare two filenames using natural sorting.  Numeric runs are
    // compared as integers and other characters case‑insensitively.
    const QString sa = a.fileName();
    const QString sb = b.fileName();
    const auto toLower = [](QChar ch) {
        return ch.toLower();
    };
    int ia = 0;
    int ib = 0;
    while (ia < sa.size() && ib < sb.size()) {
        QChar ca = sa.at(ia);
        QChar cb = sb.at(ib);
        bool isDa = ca.isDigit();
        bool isDb = cb.isDigit();
        if (isDa && isDb) {
            // Extract full numeric substrings
            int startA = ia;
            int startB = ib;
            while (ia < sa.size() && sa.at(ia).isDigit()) ++ia;
            while (ib < sb.size() && sb.at(ib).isDigit()) ++ib;
            const QString numAStr = sa.mid(startA, ia - startA);
            const QString numBStr = sb.mid(startB, ib - startB);
            bool okA = false;
            bool okB = false;
            // Use 64‑bit conversion to handle large numbers
            qlonglong numA = numAStr.toLongLong(&okA);
            qlonglong numB = numBStr.toLongLong(&okB);
            if (okA && okB) {
                if (numA != numB) {
                    return numA < numB;
                }
            } else {
                // Fallback to lexicographic comparison
                int cmp = QString::compare(numAStr, numBStr, Qt::CaseInsensitive);
                if (cmp != 0) {
                    return cmp < 0;
                }
            }
            // If numbers are equal, continue comparison
        } else {
            // Compare characters case‑insensitively
            QChar lca = ca.toLower();
            QChar lcb = cb.toLower();
            if (lca != lcb) {
                return lca < lcb;
            }
            ++ia;
            ++ib;
        }
    }
    // When prefixes are equal, shorter string sorts first
    return sa.size() < sb.size();
}

void PhotoTriageWindow::loadSourceDirectory(const QString &directory)
{
    QDir dir(directory);
    if (!dir.exists()) {
        QMessageBox::warning(this, tr("Invalid Directory"), tr("%1 is not a valid directory.").arg(directory));
        return;
    }
    // Collect image files with supported extensions
    const QStringList exts = {"*.jpg", "*.jpeg", "*.png", "*.bmp", "*.gif", "*.tif",
                               "*.tiff", "*.webp", "*.avif"};
    QFileInfoList fileList;
    for (const QString &pattern : exts) {
        fileList.append(dir.entryInfoList(QStringList() << pattern, QDir::Files | QDir::NoSymLinks, QDir::Name));
    }
    // Remove duplicates and sort naturally
    std::vector<QFileInfo> files;
    files.reserve(fileList.size());
    for (const QFileInfo &fi : fileList) {
        files.push_back(fi);
    }
    std::sort(files.begin(), files.end(), &PhotoTriageWindow::naturalLess);

    m_images = std::move(files);
    m_currentIndex = m_images.empty() ? -1 : 0;

    m_sourceDir = directory;
    // Prepare destination directories (siblings of source)
    m_keepDir = QDir(directory).filePath("keep");
    m_discardDir = QDir(directory).filePath("discard");

    // Ensure directories exist
    QDir().mkpath(m_keepDir);
    QDir().mkpath(m_discardDir);

    // Reset state
    m_preloaded.clear();
    m_undoStack.clear();
    m_statusBar->clearMessage();

    displayCurrentImage();
    preloadAhead();
}

void PhotoTriageWindow::displayCurrentImage()
{
    if (m_currentIndex < 0 || m_currentIndex >= static_cast<int>(m_images.size())) {
        m_imageLabel->clear();
        m_imageLabel->setText(tr("No images."));
        m_statusBar->showMessage(QString());
        return;
    }
    const QFileInfo &fi = m_images.at(m_currentIndex);
    QImage image;
    // Use preloaded image if available
    if (m_preloaded.contains(m_currentIndex)) {
        image = m_preloaded.take(m_currentIndex);
    } else {
        image.load(fi.filePath());
    }
    QPixmap pixmap = QPixmap::fromImage(image);
    if (!pixmap.isNull()) {
        QPixmap scaled = pixmap.scaled(m_imageLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        m_imageLabel->setPixmap(scaled);
        m_imageLabel->setText(QString());
    } else {
        m_imageLabel->setText(tr("Unable to load image"));
    }
    // Update status bar
    m_statusBar->showMessage(tr("%1/%2 – %3").arg(m_currentIndex + 1).arg(m_images.size()).arg(fi.fileName()));
}

void PhotoTriageWindow::preloadAhead()
{
    // unload anything that is now behind the current image
    for (auto it = m_preloaded.begin(); it != m_preloaded.end(); ) {
        if (it.key() <= m_currentIndex)      // keep current or newer only
            it = m_preloaded.erase(it);
        else
            ++it;
    }

    // Start loaders for a window of PRELOAD_DEPTH images ahead
    for (int i = m_currentIndex + 1;
         i <= m_currentIndex + PRELOAD_DEPTH && i < m_images.size();
         ++i)
    {
        if (m_preloaded.contains(i) || m_loading.contains(i))
            continue;                       // already ready or in progress

        const QFileInfo &fi = m_images.at(i);
        ImageLoader *loader = new ImageLoader(i, fi.filePath(), this);
        connect(loader, &ImageLoader::loaded,
                this,     &PhotoTriageWindow::onImagePreloaded,
                Qt::QueuedConnection);
        connect(loader, &QThread::finished,
                loader,  &QObject::deleteLater); // clean up automatically
        m_loading.insert(i);
        loader->start();
    }
}


void PhotoTriageWindow::onImagePreloaded(int index, const QImage &image)
{
    m_preloaded.insert(index, image);
    m_loading.remove(index);
}

void PhotoTriageWindow::performMove(const QString &action)
{
    if (m_currentIndex < 0 || m_currentIndex >= static_cast<int>(m_images.size())) {
        return;
    }
    const QFileInfo fi = m_images.at(m_currentIndex);
    QString destDirPath;
    if (action == QLatin1String("keep")) {
        destDirPath = m_keepDir;
    } else if (action == QLatin1String("discard")) {
        destDirPath = m_discardDir;
    } else {
        return;
    }
    QDir destDir(destDirPath);
    if (!destDir.exists()) {
        destDir.mkpath(".");
    }
    QString destPath = destDir.filePath(fi.fileName());
    // Ensure unique filename
    if (QFileInfo::exists(destPath)) {
        QString stem = fi.completeBaseName();
        QString suffix = fi.suffix();
        int counter = 1;
        QString candidate;
        do {
            candidate = destDir.filePath(QStringLiteral("%1_%2.%3").arg(stem).arg(counter).arg(suffix));
            counter++;
        } while (QFileInfo::exists(candidate));
        destPath = candidate;
    }
    // Perform move
    if (!QFile::rename(fi.filePath(), destPath)) {
        QMessageBox::critical(this, tr("Error Moving File"), tr("Could not move %1 to %2").arg(fi.filePath(), destPath));
        return;
    }
    // Record undo info
    MoveAction actionInfo;
    actionInfo.originalPath = fi.filePath();
    actionInfo.destinationPath = destPath;
    actionInfo.index = m_currentIndex;
    m_undoStack.push_back(actionInfo);
    if (static_cast<int>(m_undoStack.size()) > MAX_UNDO) {
        m_undoStack.pop_front();
    }
    // Remove from list
    m_images.erase(m_images.begin() + m_currentIndex);
    // Adjust index to show next image
    if (m_currentIndex >= static_cast<int>(m_images.size())) {
        m_currentIndex = static_cast<int>(m_images.size()) - 1;
    }
    // Clear preloaded cache because ordering has changed
    m_preloaded.clear();
    displayCurrentImage();
    preloadAhead();
}

void PhotoTriageWindow::handleMoveKeep()
{
    performMove(QStringLiteral("keep"));
}

void PhotoTriageWindow::handleMoveReject()
{
    performMove(QStringLiteral("discard"));
}

void PhotoTriageWindow::undoLastAction()
{
    if (m_undoStack.empty()) {
        m_statusBar->showMessage(tr("Nothing to undo."));
        return;
    }
    MoveAction action = m_undoStack.back();
    m_undoStack.pop_back();
    // Move file back to original location
    // Ensure destination directory exists
    QDir origDir = QFileInfo(action.originalPath).absoluteDir();
    if (!origDir.exists()) {
        origDir.mkpath(".");
    }
    if (!QFile::rename(action.destinationPath, action.originalPath)) {
        QMessageBox::critical(this, tr("Error Undoing File Move"), tr("Could not restore %1 to %2").arg(action.destinationPath, action.originalPath));
        return;
    }
    // Reinsert file into list
    int insertIndex = action.index;
    if (insertIndex < 0) {
        insertIndex = 0;
    }
    if (insertIndex > static_cast<int>(m_images.size())) {
        insertIndex = static_cast<int>(m_images.size());
    }
    m_images.insert(m_images.begin() + insertIndex, QFileInfo(action.originalPath));
    // Update current index
    m_currentIndex = insertIndex;
    m_preloaded.clear();
    displayCurrentImage();
    preloadAhead();
}
