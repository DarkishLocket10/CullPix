// phototriagewindow.cpp
//
// Implements the PhotoTriageWindow class. This class provides
// the main user interface for browsing and culling images. The
// implementation focuses on keep/reject behaviour and includes a
// basic undo stack.

#include "phototriagewindow.h"
#include "imageloader.h"
#include "imageview.h"
#include "fileworker.h"

#include <QLabel>
#include <QPushButton>
#include <QStatusBar>
#include <QProgressBar>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSplitter>
#include <QListWidget>
#include <QFileDialog>
#include <QShortcut>
#include <QKeySequence>
#include <QToolBar>
#include <QDockWidget>
#include <QMenuBar>
#include <QMenu>
#include <QActionGroup>
#include <QListView>
#include <QSlider>
#include <QMessageBox>
#include <QResizeEvent>
#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QApplication>
#include <QTimer>
#include <QThread>
#include <QIcon>
#include <QPixmap>
#include <QFileIconProvider>
#include <QSet>
#include <QQueue>

// RAW decoding is handled entirely on the worker thread by ImageLoader, so
// this translation unit no longer needs RawLoader directly.
#include <cctype>
#include <QVector>



PhotoTriageWindow::PhotoTriageWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("CullPix"));
    resize(1000, 700);

    QString appStyle = R"(
        QMainWindow {
            background-color: #121212;
            color: #E0E0E0;
            font-family: "SF Pro Text", "Helvetica Neue", "Segoe UI", Arial, sans-serif;
            font-size: 14px;
        }
        QStatusBar {
            background-color: #1E1E1E;
            color: #B8BCC2;
            border-top: 1px solid #2C2C2C;
        }
        QStatusBar::item { border: none; }
        QListWidget {
            background-color: #1A1A1A;
            color: #CCCCCC;
            border: none;
        }
        QListWidget::item {
            padding: 8px;
            margin: 2px 4px;
            border-radius: 6px;
        }
        QListWidget::item:selected {
            background-color: #264653;
            color: #FFFFFF;
        }
        QProgressBar {
            background-color: #2C2C2C;
            border: none;
            border-radius: 3px;
        }
        QProgressBar::chunk {
            background-color: #2A9D8F;
            border-radius: 3px;
        }
    )";
    qApp->setStyleSheet(appStyle);

    // Set up the side file browser and central image display. Use a
    // splitter so the user can adjust the space between the two panes. The
    // left pane shows a thumbnail list of images; the right pane displays
    // the currently selected photo.
    m_fileListWidget = new QListWidget(this);
    m_fileListWidget->setViewMode(QListView::IconMode);      // grid of thumbnails
    m_fileListWidget->setMovement(QListView::Static);        // no drag-rearrange
    m_fileListWidget->setResizeMode(QListView::Adjust);      // re-flow the grid on resize
    m_fileListWidget->setUniformItemSizes(true);
    m_fileListWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    m_fileListWidget->setIconSize(QSize(96, 96));
    m_fileListWidget->setGridSize(QSize(116, 108));   // names hidden by default
    connect(m_fileListWidget, &QListWidget::currentRowChanged,
            this, &PhotoTriageWindow::onFileListSelectionChanged);

    // Zoomable/pannable image view: renders at physical pixels (sharp on
    // high-DPI), pinch / Cmd+wheel / double-click to zoom, 1:1 for real pixels.
    m_imageView = new ImageView(this);

    setCentralWidget(m_imageView);

    // The timeline (file browser) lives in a dock widget so the user can move
    // it to any edge, float it as its own window, or hide it. It lays photos
    // out as a wrapping grid (or a single-row filmstrip when docked top/bottom)
    // and carries a thumbnail-size slider that travels with it when floated.
    m_timelineDock = new QDockWidget(tr("Timeline"), this);
    m_timelineDock->setObjectName(QStringLiteral("timelineDock"));
    m_timelineDock->setAllowedAreas(Qt::AllDockWidgetAreas);
    m_timelineDock->setFeatures(QDockWidget::DockWidgetMovable
                                | QDockWidget::DockWidgetFloatable
                                | QDockWidget::DockWidgetClosable);

    QWidget *timelinePanel = new QWidget;
    QVBoxLayout *tlLayout = new QVBoxLayout(timelinePanel);
    tlLayout->setContentsMargins(0, 0, 0, 0);
    tlLayout->setSpacing(0);
    tlLayout->addWidget(m_fileListWidget, 1);

    QWidget *sizeBar = new QWidget;
    QHBoxLayout *sizeRow = new QHBoxLayout(sizeBar);
    sizeRow->setContentsMargins(8, 4, 8, 4);
    QLabel *sizeLabel = new QLabel(tr("Size"));
    sizeLabel->setStyleSheet(QStringLiteral("color:#9AA0A6;"));
    m_thumbSlider = new QSlider(Qt::Horizontal);
    m_thumbSlider->setRange(64, THUMB_PX);
    m_thumbSlider->setToolTip(tr("Thumbnail size"));
    connect(m_thumbSlider, &QSlider::valueChanged, this, [this](int v) {
        m_fileListWidget->setIconSize(QSize(v, v));
        m_fileListWidget->setGridSize(QSize(v + 20, v + (m_showNames ? 36 : 12)));
    });
    m_thumbSlider->setValue(96);   // fires the slot to set the initial grid metrics
    sizeRow->addWidget(sizeLabel);
    sizeRow->addWidget(m_thumbSlider, 1);
    tlLayout->addWidget(sizeBar);
    m_timelineDock->setWidget(timelinePanel);

    addDockWidget(Qt::LeftDockWidgetArea, m_timelineDock);
    // Re-dock instead of hiding when the floating timeline window is closed.
    m_timelineDock->installEventFilter(this);
    connect(m_timelineDock, &QDockWidget::dockLocationChanged,
            this, &PhotoTriageWindow::updateTimelineLayout);
    connect(m_timelineDock, &QDockWidget::topLevelChanged,
            this, &PhotoTriageWindow::updateTimelineLayout);
    updateTimelineLayout();

    // Status bar
    m_statusBar = statusBar();
    m_statusBar->setSizeGripEnabled(false);

    // Triage progress on the right of the status bar: a counts label and a slim
    // progress bar (kept + rejected out of the folder's total).
    m_countsLabel = new QLabel(this);
    m_countsLabel->setStyleSheet(QStringLiteral("color:#9AA0A6; padding:0 10px;"));
    m_progressBar = new QProgressBar(this);
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedSize(150, 6);
    m_progressBar->setRange(0, 1);
    m_progressBar->setValue(0);
    m_statusBar->addPermanentWidget(m_countsLabel);
    m_statusBar->addPermanentWidget(m_progressBar);

    // Buttons with contemporary styling. Each button uses a distinct accent
    // color to convey its purpose. A green tone is used for "Keep", a
    // salmon/red tone for "Reject", and a soft orange for "Undo".
    m_keepButton = new QPushButton(tr("Keep (Z)"));
    m_rejectButton = new QPushButton(tr("Reject (X)"));
    m_undoButton = new QPushButton(tr("Undo (U)"));

    applyButtonStyle();   // accent colors by default; monochrome via Options

    m_openButton = new QPushButton(tr("Open Folder..."));
    m_openButton->setToolTip(tr("Open a folder of images (Ctrl+O)"));
    m_openButton->setShortcut(QKeySequence::Open);
    m_openButton->setAutoDefault(false);

    m_openButton->setStyleSheet("QPushButton { background-color: #272b33; color: #FFFFFF; "
                                "border: none; border-radius: 6px; padding: 8px 16px; "
                                "font-weight: 600; } "
                                "QPushButton:hover { background-color: #23272e; } "
                                "QPushButton:pressed { background-color: #1f2329; }");


    connect(m_openButton, &QPushButton::clicked, this, &PhotoTriageWindow::chooseSourceFolder);

    // Connect button clicks to handlers
    connect(m_keepButton, &QPushButton::clicked, this, &PhotoTriageWindow::handleMoveKeep);
    connect(m_rejectButton, &QPushButton::clicked, this, &PhotoTriageWindow::handleMoveReject);
    connect(m_undoButton, &QPushButton::clicked, this, &PhotoTriageWindow::undoLastAction);

    // Cull action buttons grouped together so "Show Buttons" can hide just
    // these — the Timeline + Options controls stay on the bar, so settings
    // remain reachable in-window even with the action buttons hidden.
    m_actionButtons = new QWidget(this);
    QHBoxLayout *actions = new QHBoxLayout(m_actionButtons);
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(12);
    actions->addWidget(m_openButton);
    actions->addWidget(m_keepButton);
    actions->addWidget(m_rejectButton);
    actions->addWidget(m_undoButton);

    QWidget *toolbarWidget = new QWidget(this);
    QHBoxLayout *hbox = new QHBoxLayout(toolbarWidget);
    hbox->setContentsMargins(10, 8, 10, 8);
    hbox->setSpacing(12);
    hbox->addWidget(m_actionButtons);
    hbox->addStretch(1);

    // Quick show/hide for the timeline, right-aligned on the toolbar.
    m_timelineButton = new QPushButton(tr("Timeline"));
    m_timelineButton->setCheckable(true);
    m_timelineButton->setChecked(true);
    m_timelineButton->setAutoDefault(false);
    m_timelineButton->setStyleSheet("QPushButton { background-color: #272b33; color: #FFFFFF; "
                                     "border: none; border-radius: 6px; padding: 8px 16px; "
                                     "font-weight: 600; } "
                                     "QPushButton:checked { background-color: #2A9D8F; } "
                                     "QPushButton:hover { background-color: #23272e; }");
    connect(m_timelineButton, &QPushButton::clicked, this,
            [this]{ m_timelineDock->setVisible(!m_timelineDock->isVisible()); });
    connect(m_timelineDock, &QDockWidget::visibilityChanged,
            m_timelineButton, &QPushButton::setChecked);
    hbox->addWidget(m_timelineButton);

    toolbarWidget->setLayout(hbox);
    m_toolBar = new QToolBar(this);
    m_toolBar->setMovable(false);
    m_toolBar->addWidget(toolbarWidget);
    addToolBar(Qt::BottomToolBarArea, m_toolBar);

    // ---- Menu bar (native macOS menu; always reachable even when chrome is hidden) ----
    QMenu *fileMenu = menuBar()->addMenu(tr("File"));
    QAction *openMenuAct = fileMenu->addAction(tr("Open Folder…"));
    connect(openMenuAct, &QAction::triggered, this, &PhotoTriageWindow::chooseSourceFolder);

    QMenu *viewMenu = menuBar()->addMenu(tr("Options"));

    // Timeline position (Top / Bottom / Left / Right).
    QMenu *posMenu = viewMenu->addMenu(tr("Timeline Position"));
    QActionGroup *posGroup = new QActionGroup(this);
    const struct { const char *label; Qt::DockWidgetArea area; } kPositions[] = {
        { "Top",    Qt::TopDockWidgetArea },
        { "Bottom", Qt::BottomDockWidgetArea },
        { "Left",   Qt::LeftDockWidgetArea },
        { "Right",  Qt::RightDockWidgetArea },
    };
    for (const auto &p : kPositions) {
        QAction *a = posMenu->addAction(tr(p.label));
        a->setCheckable(true);
        posGroup->addAction(a);
        if (p.area == Qt::LeftDockWidgetArea)
            a->setChecked(true);
        const Qt::DockWidgetArea area = p.area;
        connect(a, &QAction::triggered, this, [this, area]{
            addDockWidget(area, m_timelineDock);
            m_timelineDock->show();
        });
        m_timelinePosActions.insert(static_cast<int>(area), a);
    }

    QAction *floatAct = viewMenu->addAction(tr("Timeline in Separate Window"));
    connect(floatAct, &QAction::triggered, this, [this]{
        m_timelineDock->setFloating(true);
        m_timelineDock->show();
    });

    QAction *showTimelineAct = m_timelineDock->toggleViewAction();
    showTimelineAct->setText(tr("Show Timeline"));
    showTimelineAct->setShortcut(QKeySequence(QStringLiteral("T")));
    viewMenu->addAction(showTimelineAct);

    viewMenu->addSeparator();

    // Hide the whole bottom bar to reclaim photo space (Options stays reachable
    // via the gear overlay and the macOS menu bar).
    QAction *showButtonsAct = m_toolBar->toggleViewAction();
    showButtonsAct->setText(tr("Show Buttons"));
    viewMenu->addAction(showButtonsAct);

    QAction *showInfoAct = viewMenu->addAction(tr("Show Info Bar"));
    showInfoAct->setCheckable(true);
    showInfoAct->setChecked(true);
    connect(showInfoAct, &QAction::toggled, this,
            [this](bool on){ m_statusBar->setVisible(on); });

    QAction *showNamesAct = viewMenu->addAction(tr("Show Image Names"));
    showNamesAct->setCheckable(true);
    showNamesAct->setChecked(m_showNames);   // hidden by default
    connect(showNamesAct, &QAction::toggled, this, [this](bool on) {
        m_showNames = on;
        const int n = m_fileListWidget->count();
        for (int i = 0; i < n && i < static_cast<int>(m_images.size()); ++i)
            if (QListWidgetItem *it = m_fileListWidget->item(i))
                it->setText(on ? m_images[i].fileName() : QString());
        const int v = m_thumbSlider->value();
        m_fileListWidget->setGridSize(QSize(v + 20, v + (on ? 36 : 12)));
    });

    QAction *monoAct = viewMenu->addAction(tr("Monochrome Buttons"));
    monoAct->setCheckable(true);
    monoAct->setChecked(m_monochrome);
    connect(monoAct, &QAction::toggled, this, [this](bool on) {
        m_monochrome = on;
        applyButtonStyle();
    });

    // Thumbnail size presets (the timeline's slider gives fine control).
    QMenu *sizeMenu = viewMenu->addMenu(tr("Thumbnail Size"));
    const struct { const char *label; int px; } kSizes[] = {
        { "Small", 72 }, { "Medium", 104 }, { "Large", 150 },
    };
    for (const auto &s : kSizes) {
        const int px = s.px;
        sizeMenu->addAction(tr(s.label), this, [this, px]{
            if (m_thumbSlider) m_thumbSlider->setValue(px);
        });
    }

    // Options live in a small, always-visible gear tucked into the image's
    // top-right corner, so they're reachable even when the toolbar is hidden
    // and don't take up photo real estate.
    QPushButton *gearButton = new QPushButton(QStringLiteral("⚙"));
    gearButton->setMenu(viewMenu);
    gearButton->setFixedSize(30, 30);
    gearButton->setCursor(Qt::PointingHandCursor);
    gearButton->setToolTip(tr("Options"));
    gearButton->setStyleSheet("QPushButton { background-color: rgba(28,28,28,150); color:#E6E6E6; "
                              "border:none; border-radius:15px; font-size:15px; } "
                              "QPushButton:hover { background-color: rgba(58,63,71,215); } "
                              "QPushButton::menu-indicator { width:0px; }");
    m_imageView->setCornerWidget(gearButton);

    // Shortcuts
    // new QShortcut(QKeySequence(QStringLiteral("Z")), this, SLOT(handleMoveKeep()));
    // new QShortcut(QKeySequence(QStringLiteral("X")), this, SLOT(handleMoveReject()));
    auto sKeep = new QShortcut(QKeySequence(QStringLiteral("Z")), this);
    sKeep->setAutoRepeat(false);
    sKeep->setContext(Qt::ApplicationShortcut);
    connect(sKeep, &QShortcut::activated, this, &PhotoTriageWindow::handleMoveKeep);

    auto sReject = new QShortcut(QKeySequence(QStringLiteral("X")), this);
    sReject->setAutoRepeat(false);
    sReject->setContext(Qt::ApplicationShortcut);
    connect(sReject, &QShortcut::activated, this, &PhotoTriageWindow::handleMoveReject);


    new QShortcut(QKeySequence(QStringLiteral("U")), this, SLOT(undoLastAction()));
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+Z")), this, SLOT(undoLastAction()));
    new QShortcut(QKeySequence(QStringLiteral("O")), this, SLOT(chooseSourceFolder()));
    // Arrow key shortcuts to browse images without performing any action
    new QShortcut(QKeySequence(Qt::Key_Right), this, SLOT(goToNextImage()));
    new QShortcut(QKeySequence(Qt::Key_Left), this, SLOT(goToPreviousImage()));

    // Ask for source folder on startup after event loop starts
    QTimer::singleShot(0, this, &PhotoTriageWindow::chooseSourceFolder);

    // Initialise asynchronous file worker
    m_fileWorker = new FileWorker();

    // Show the empty-state prompt until a folder is chosen.
    displayCurrentImage();
}

PhotoTriageWindow::~PhotoTriageWindow()
{
    // Stop and delete the file worker
    if (m_fileWorker) {
        m_fileWorker->stop();
        delete m_fileWorker;
        m_fileWorker = nullptr;
    }
}

void PhotoTriageWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    // The image view re-fits itself on resize (see ImageView::resizeEvent),
    // so there's no need to rebuild the whole display here.
}

void PhotoTriageWindow::closeEvent(QCloseEvent *event)
{
    // Stop file worker when closing
    if (m_fileWorker) {
        m_fileWorker->stop();
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

static inline bool isSep(QChar c) {
    return c == QChar('-') || c == QChar('_') || c == QChar(' ') || c == QChar('.');
}

struct Token {
    bool isNum;
    qint64 num; // valid if isNum == true
    int start; // begin index in source string for text
    int len; // length of text, for numbers, this holds the digit count for tie breakers.
};

struct SortKey{
    QString base; // completeBaseName() (owned, so indicies remain valid)
    QString ext; // suffic()
    QVector<Token> tokens; // Tokens for 'base'
};

static void buildTokens(const QString& s, QVector<Token>& out) {
    out.clear();
    const int n = s.size();
    int i = 0;

    auto skipSeps = [&](int &k){
        while (k < n && isSep(s[k])) ++k;
    };

    while (i < n) {
        skipSeps(i);
        if (i >= n) break;

        const QChar c = s[i];
        if (c.isDigit()) {
            qint64 v = 0;
            int start = i;
            while (i < n && s[i].isDigit()) {
                v = v * 10 + (s[i].unicode() - '0');
                ++i;
            }
            Token t;
            t.isNum = true;
            t.num = v;
            t.start = start;
            t.len = i - start; // here is our digit count for the tie breaker. (ex: 2 vs 002)
            out.push_back(t);
        }
        else {
            int start = i;
            while (i < n && !s[i].isDigit() && !isSep(s[i])) ++i;
            Token t;
            t.isNum = false;
            t.num = 0;
            t.start = start;
            t.len = i - start;
            out.push_back(t);
        }
    }
}

// case insensitive compare of two text slices in 'src' without allocations
static int cmpTextCI(const QString& srcA, int aStart, int aLen, const QString& srcB, int bStart, int bLen) {

    const int L = qMin(aLen, bLen);

    for (int k = 0; k < L; ++k) {
        ushort ca = srcA[aStart + k].toLower().unicode();
        ushort cb = srcB[bStart + k].toLower().unicode();

        if (ca != cb) {
            return (ca < cb) ? -1 : 1;
        }
    }
    // All compared characters are equal up to the length of the shorter substring.
    // Let the caller decide based on length or other criteria; here we indicate equality.
    return 0;
}


static int cmpTokens(const SortKey& A, const SortKey& B) {
    const auto& a = A.tokens;
    const auto& b = B.tokens;
    int ia = 0, ib = 0;

    while (ia < a.size() && ib < b.size()) {
        const Token &ta = a[ia], &tb = b[ib];

        if (ta.isNum && tb.isNum) {
            if (ta.num != tb.num) return (ta.num < tb.num) ? -1 : 1;
            // same numeric value; prefer shorter digit run (e.g., "2" < "002")
            if (ta.len != tb.len) return (ta.len < tb.len) ? -1 : 1;
        } else if (!ta.isNum && !tb.isNum) {
            int c = cmpTextCI(A.base, ta.start, ta.len, B.base, tb.start, tb.len);
            if (c != 0) return c;
        } else {
            // Decide whether text < number or number < text. Explorer-like feel prefers text first.
            return ta.isNum ? 1 : -1;
        }
        ++ia; ++ib;
    }

    // prefix rule: fewer tokens wins.
    if (ia != ib) return (ia < ib) ? -1 : 1;
    // Tie-break: extension (case-insensitive, simplistic is fine)
    int c = cmpTextCI(A.ext, 0, A.ext.size(), B.ext, 0, B.ext.size());
    if (c != 0) return c;

    // Final fallback: compare full fileName (stable order)
    // (This is very rarely hit; helps keep sort stable across equal keys.)
    return 0;
}

static bool naturalLessKeyed(const std::pair<QFileInfo, SortKey>& A,
                             const std::pair<QFileInfo, SortKey>& B) {
    const int c = cmpTokens(A.second, B.second);
    if (c != 0) return c < 0;

    // Fallback on full filename CI compare to stabilize exact ties:
    const QString fa = A.first.fileName();
    const QString fb = B.first.fileName();
    const int L = qMin(fa.size(), fb.size());
    for (int i = 0; i < L; ++i) {
        ushort ca = fa[i].toLower().unicode();
        ushort cb = fb[i].toLower().unicode();
        if (ca != cb) return ca < cb;
    }
    return fa.size() < fb.size();
}



void PhotoTriageWindow::loadSourceDirectory(const QString &directory)
{
    QDir dir(directory);
    if (!dir.exists()) {
        QMessageBox::warning(this, tr("Invalid Directory"), tr("%1 is not a valid directory.").arg(directory));
        return;
    }
    // Collect image files with supported extensions.  Pass all filters at once to
    // avoid duplicates (e.g. *.jpg and *.jpeg matching the same file).  The
    // resulting list is sorted lexically by name; we'll sort naturally below.
    // Supported file extensions.  Include common RAW formats alongside
    // standard image types.  Use both lowercase and uppercase patterns so
    // case‑sensitive filesystems are handled.  When adding new RAW types
    // here ensure the detection logic in ImageLoader/RawLoader matches.
    const QStringList exts = {
        "*.jpg", "*.jpeg", "*.png", "*.bmp", "*.gif",
        "*.tif", "*.tiff", "*.webp", "*.avif",
        // RAW formats (Sony, Canon, Nikon, Fujifilm, Panasonic, Leica, Olympus, Pentax, Samsung, Adobe, generic)
        "*.arw", "*.ARW", "*.cr2", "*.CR2", "*.cr3", "*.CR3",
        "*.nef", "*.NEF", "*.nrw", "*.NRW", "*.raf", "*.RAF",
        "*.rw2", "*.RW2", "*.rwl", "*.RWL", "*.orf", "*.ORF",
        "*.pef", "*.PEF", "*.srw", "*.SRW", "*.dng", "*.DNG",
        "*.raw", "*.RAW"
    };
    QFileInfoList fileList = dir.entryInfoList(exts, QDir::Files | QDir::NoSymLinks, QDir::Name);

    // Transfer to std::vector for natural sorting and deduplicate by absolute path
    std::vector<QFileInfo> files;
    files.reserve(fileList.size());
    QSet<QString> seen;
    for (const QFileInfo &fi : fileList) {
        const QString abs = fi.absoluteFilePath();
        if (!seen.contains(abs)) {
            files.push_back(fi);
            seen.insert(abs);
        }
    }
    // std::sort(files.begin(), files.end(), &PhotoTriageWindow::naturalLess);
    // Pre-tokenize once, then sort on the keys (fast)
    std::vector<std::pair<QFileInfo, SortKey>> keyed;
    keyed.reserve(files.size());
    for (const QFileInfo& fi : files) {
        SortKey k;
        k.base = fi.completeBaseName();
        k.ext  = fi.suffix();
        buildTokens(k.base, k.tokens);
        keyed.emplace_back(fi, std::move(k));
    }

    std::sort(keyed.begin(), keyed.end(), &naturalLessKeyed);

    files.clear();
    files.reserve(keyed.size());
    for (const auto &pair : keyed) {
        files.push_back(pair.first);
    }

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
    m_loading.clear();
    m_fullRequested.clear();
    m_undoStack.clear();
    m_keptCount = 0;
    m_rejectedCount = 0;
    m_totalCount = static_cast<int>(m_images.size());
    m_statusBar->clearMessage();

    displayCurrentImage();
    // Begin preloading immediately so the next few images are ready before the user
    // navigates.  This call will schedule ImageLoader instances via
    // ensurePreloadWindow().
    ensurePreloadWindow();

    // Populate the side list with the new set of images.  Building the list
    // immediately after loading the directory ensures that the thumbnails are
    // available before the user begins navigating.  Without this call the list
    // would still show entries from a previous directory.
    populateFileList();
}

void PhotoTriageWindow::displayCurrentImage()
{
    refreshProgress();
    if (m_currentIndex < 0 || m_currentIndex >= static_cast<int>(m_images.size())) {
        m_imageView->showMessage(tr("No images to cull\n\n"
                                    "Press O or click “Open Folder” to choose a folder\n\n"
                                    "Z keep     ·     X reject     ·     U undo     ·     ← → browse"));
        m_statusBar->showMessage(QString());
        // Clear selection in file list when there are no images
        if (m_fileListWidget) {
            m_fileListWidget->blockSignals(true);
            m_fileListWidget->setCurrentRow(-1);
            m_fileListWidget->blockSignals(false);
        }
        return;
    }
    const QFileInfo &fi = m_images.at(m_currentIndex);
    const QString key = fi.absoluteFilePath();

    // Show the best image we have right now — a full-quality decode, a fast RAW
    // preview (to be upgraded below), or the thumbnail / a "Loading…" message as
    // an instant placeholder. The view renders at physical pixels and supports
    // pinch / 1:1 zoom. We never evict here; ensurePreloadWindow() manages the
    // sliding window, which keeps back-and-forth navigation (and undo) instant.
    if (m_preloaded.contains(key)) {
        m_imageView->setImage(m_preloaded.value(key).image);
    } else if (m_thumbnailCache.contains(key)) {
        m_imageView->setImage(m_thumbnailCache.value(key).toImage());
    } else {
        m_imageView->showMessage(tr("Loading…"));
    }

    // Ensure the *current* image reaches full display quality: a full-resolution
    // decode (JPEG) or full demosaic (RAW), off the UI thread, swapped in by
    // onImagePreloaded(). This runs for a cache miss and for a cached fast RAW
    // preview alike — but only ever for the image being viewed, never the
    // prefetch ring (so a RAW folder won't fire many concurrent demosaics).
    const bool haveFull = m_preloaded.contains(key) && m_preloaded.value(key).full;
    if (!haveFull && !m_fullRequested.contains(key)) {
        ImageLoader *ldr = new ImageLoader(m_currentIndex, key, this, QSize(), /*fullQuality=*/true);
        connect(ldr, &ImageLoader::loaded,
                this, &PhotoTriageWindow::onImagePreloaded,
                Qt::QueuedConnection);
        connect(ldr, &QThread::finished, ldr, &QObject::deleteLater);
        m_fullRequested.insert(key);
        ldr->start();
    }
    // Update status bar (filename; counts + progress are shown on the right)
    m_statusBar->showMessage(fi.fileName());

    // Highlight the current item in the side list.  Blocking signals prevents
    // triggering onFileListSelectionChanged recursively.
    if (m_fileListWidget) {
        m_fileListWidget->blockSignals(true);
        m_fileListWidget->setCurrentRow(m_currentIndex);
        m_fileListWidget->scrollToItem(m_fileListWidget->currentItem(), QAbstractItemView::PositionAtCenter);
        m_fileListWidget->blockSignals(false);
    }
}

void PhotoTriageWindow::applyButtonStyle()
{
    auto sheet = [](const char *base, const char *hover, const char *pressed) {
        return QStringLiteral("QPushButton { background-color:%1; color:#FFFFFF; border:none; "
                              "border-radius:6px; padding:8px 16px; font-weight:600; } "
                              "QPushButton:hover { background-color:%2; } "
                              "QPushButton:pressed { background-color:%3; }")
            .arg(QString::fromLatin1(base))
            .arg(QString::fromLatin1(hover))
            .arg(QString::fromLatin1(pressed));
    };
    if (m_monochrome) {
        const QString s = sheet("#3A3F47", "#33373E", "#2B2F35");
        m_keepButton->setStyleSheet(s);
        m_rejectButton->setStyleSheet(s);
        m_undoButton->setStyleSheet(s);
    } else {
        m_keepButton->setStyleSheet(sheet("#2A9D8F", "#21867A", "#1E745F"));
        m_rejectButton->setStyleSheet(sheet("#E76F51", "#CF6045", "#B25037"));
        m_undoButton->setStyleSheet(sheet("#F4A261", "#D68F54", "#BB7A46"));
    }
}

void PhotoTriageWindow::refreshProgress()
{
    if (!m_countsLabel || !m_progressBar)
        return;
    if (m_totalCount > 0) {
        m_countsLabel->setText(tr("%1 kept    ·    %2 rejected    ·    %3 left")
                                   .arg(m_keptCount)
                                   .arg(m_rejectedCount)
                                   .arg(static_cast<int>(m_images.size())));
        m_progressBar->setRange(0, m_totalCount);
        m_progressBar->setValue(m_keptCount + m_rejectedCount);
        m_countsLabel->show();
        m_progressBar->show();
    } else {
        m_countsLabel->clear();
        m_progressBar->hide();
    }
}

void PhotoTriageWindow::updateTimelineLayout()
{
    if (!m_fileListWidget || !m_timelineDock)
        return;
    const bool floating = m_timelineDock->isFloating();
    const Qt::DockWidgetArea area = dockWidgetArea(m_timelineDock);
    // Single-row filmstrip when docked top/bottom; a wrapping grid otherwise
    // (left/right docks and the floating window) so widening lays out a grid.
    const bool filmstrip = !floating
        && (area == Qt::TopDockWidgetArea || area == Qt::BottomDockWidgetArea);
    m_fileListWidget->setFlow(QListView::LeftToRight);
    m_fileListWidget->setWrapping(!filmstrip);
    if (filmstrip) {
        m_fileListWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        m_fileListWidget->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    } else {
        m_fileListWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_fileListWidget->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    }
    // Keep the Options-menu position radio in sync when the dock is dragged.
    if (!floating) {
        if (QAction *a = m_timelinePosActions.value(static_cast<int>(area), nullptr))
            a->setChecked(true);
    }
}

bool PhotoTriageWindow::eventFilter(QObject *obj, QEvent *event)
{
    // Closing the floating timeline window re-docks it instead of hiding it.
    if (obj == m_timelineDock && event->type() == QEvent::Close
        && m_timelineDock->isFloating()) {
        m_timelineDock->setFloating(false);
        m_timelineDock->show();
        event->ignore();
        return true;
    }
    return QMainWindow::eventFilter(obj, event);
}

int PhotoTriageWindow::indexFromPath(const QString &path) const
{
    // Linear scan (O(N)) – perfectly fine for a few thousand images.
    for (int i = 0; i < static_cast<int>(m_images.size()); ++i)
        if (m_images[i].absoluteFilePath() == path)
            return i;
    return -1;          // not found (shouldn’t normally happen)
}


void PhotoTriageWindow::ensurePreloadWindow()
{
    if (m_currentIndex < 0 || m_currentIndex >= static_cast<int>(m_images.size())) {
        return;
    }
    // Maintain a sliding window of preloaded images around the current index.  The
    // cache retains images within [currentIndex - PRELOAD_BACK_DEPTH, currentIndex + PRELOAD_DEPTH].
    for (auto it = m_preloaded.begin(); it != m_preloaded.end(); ) {
        const QString path = it.key();
        int idx = indexFromPath(path);
        // Keep images within the window; evict those too far behind or ahead
        if (idx < m_currentIndex - PRELOAD_BACK_DEPTH || idx > m_currentIndex + PRELOAD_DEPTH) {
            it = m_preloaded.erase(it);
        } else {
            ++it;
        }
    }
    // Preload ahead within the forward window
    for (int i = m_currentIndex + 1; i <= m_currentIndex + PRELOAD_DEPTH && i < m_images.size(); ++i) {
        const QString key = m_images.at(i).absoluteFilePath();
        if (m_preloaded.contains(key) || m_loading.contains(key)) continue;
        ImageLoader *ldr = new ImageLoader(i, key, this, QSize(), /*fullQuality=*/false);
        connect(ldr, &ImageLoader::loaded,
                this,  &PhotoTriageWindow::onImagePreloaded,
                Qt::QueuedConnection);
        connect(ldr, &QThread::finished, ldr, &QObject::deleteLater);
        m_loading.insert(key);
        ldr->start();
    }
    // Optionally preload a small number of images behind the current one to
    // facilitate smooth backward navigation.  Only start loaders for those
    // indices if not already cached or loading.
    for (int i = m_currentIndex - 1; i >= m_currentIndex - PRELOAD_BACK_DEPTH && i >= 0; --i) {
        const QString key = m_images.at(i).absoluteFilePath();
        if (m_preloaded.contains(key) || m_loading.contains(key)) continue;
        ImageLoader *ldr = new ImageLoader(i, key, this, QSize(), /*fullQuality=*/false);
        connect(ldr, &ImageLoader::loaded,
                this,  &PhotoTriageWindow::onImagePreloaded,
                Qt::QueuedConnection);
        connect(ldr, &QThread::finished, ldr, &QObject::deleteLater);
        m_loading.insert(key);
        ldr->start();
    }
}


void PhotoTriageWindow::onImagePreloaded(int index, const QString &path, const QImage &image, bool fullQuality)
{
    Q_UNUSED(index);
    // Store in the cache keyed by absolute path. Never downgrade: a fast RAW
    // preview must not overwrite a full-quality image already cached.
    const bool haveFull = m_preloaded.contains(path) && m_preloaded.value(path).full;
    if (fullQuality) {
        m_preloaded.insert(path, CachedImage{image, true});
        m_fullRequested.remove(path);
    } else if (!haveFull) {
        m_preloaded.insert(path, CachedImage{image, false});
    }
    // Clear the prefetch in-flight marker for this path.
    m_loading.remove(path);
    // If this is the image on screen — a fast preview just arrived, the
    // full-quality upgrade finished, or an un-cached photo was undone — show it
    // now. displayCurrentImage() also re-requests full quality if still needed.
    if (m_currentIndex >= 0 && m_currentIndex < static_cast<int>(m_images.size())
        && m_images.at(m_currentIndex).absoluteFilePath() == path) {
        displayCurrentImage();
    }
    ensurePreloadWindow();
}

// Initiate asynchronous thumbnail loading for list items that do not yet
// have cached thumbnails.  Uses ImageLoader with a small target size to
// reduce decoding overhead.  Loading is performed off the main thread and
// results are delivered via onThumbnailLoaded().
void PhotoTriageWindow::startThumbnailLoaders()
{
    // Do not attempt to load thumbnails if the list is empty or widget missing
    if (!m_fileListWidget)
        return;
    // Build the pending queue of indices requiring thumbnail load.  Only
    // enqueue items without a cached thumbnail and not already loading.
    m_thumbPending.clear();
    const int count = static_cast<int>(m_images.size());
    for (int i = 0; i < count; ++i) {
        const QString path = m_images.at(i).absoluteFilePath();
        if (m_thumbnailCache.contains(path))
            continue;
        if (m_thumbLoadingPaths.contains(path))
            continue;
        m_thumbPending.enqueue(i);
    }
    // Immediately start up to MAX_THUMB_CONCURRENCY loaders.  Remaining tasks
    // will be launched as earlier loads complete.  If the pending queue is
    // empty, this call has no effect.
    startNextThumbnailLoader();
}

void PhotoTriageWindow::startNextThumbnailLoader()
{
    // Launch new thumbnail loader(s) until we reach the concurrency limit or
    // run out of pending items.  Using a loop here allows us to catch up
    // quickly when multiple loads finish in rapid succession.
    while (m_thumbLoadingPaths.size() < MAX_THUMB_CONCURRENCY && !m_thumbPending.isEmpty()) {
        int index = m_thumbPending.dequeue();
        if (index < 0 || index >= static_cast<int>(m_images.size()))
            continue;
        const QString path = m_images.at(index).absoluteFilePath();
        // Skip if already cached or loading
        if (m_thumbnailCache.contains(path) || m_thumbLoadingPaths.contains(path))
            continue;
        // Launch loader
        ImageLoader *ldr = new ImageLoader(index, path, this, QSize(THUMB_PX, THUMB_PX));
        connect(ldr, &ImageLoader::loaded,
                this, &PhotoTriageWindow::onThumbnailLoaded,
                Qt::QueuedConnection);
        connect(ldr, &QThread::finished, ldr, &QObject::deleteLater);
        m_thumbLoadingPaths.insert(path);
        ldr->start();
    }
}

// Handle the completion of a thumbnail load.  Save the pixmap to the cache
// and update the file list item if it still exists.  Remove the index from
// the loading set.  The index parameter corresponds to the row in
// m_images at load time; if the list has changed since launch, the
// thumbnail may need to be discarded.
void PhotoTriageWindow::onThumbnailLoaded(int index, const QString &path, const QImage &image)
{
    Q_UNUSED(index);
    // Remove the path from the loading set.  This ensures that future
    // requests for this thumbnail can proceed if the row shifts.
    m_thumbLoadingPaths.remove(path);
    // Cache the pixmap if valid
    QPixmap pixmap = QPixmap::fromImage(image);
    if (!pixmap.isNull()) {
        m_thumbnailCache.insert(path, pixmap);
    }
    // Determine the current row of this path.  Rows may shift due to
    // keep/reject/undo operations.  If found, update the icon on the list
    // item.  Use a generic file icon if the pixmap is null.
    int row = indexFromPath(path);
    if (row >= 0 && row < m_fileListWidget->count()) {
        QListWidgetItem *item = m_fileListWidget->item(row);
        if (item) {
            if (!pixmap.isNull()) {
                item->setIcon(QIcon(pixmap));
            }
        }
    }
    // Launch the next thumbnail loader from the pending queue, if any
    startNextThumbnailLoader();
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
    // Asynchronously move file using the background worker
    FileTask task;
    task.source = fi.filePath();
    task.destination = destPath;
    if (m_fileWorker) {
        m_fileWorker->enqueue(task);
    }
    // Record undo info
    MoveAction actionInfo;
    actionInfo.originalPath = fi.filePath();
    actionInfo.destinationPath = destPath;
    actionInfo.index = m_currentIndex;
    actionInfo.kind = action;
    if (action == QLatin1String("keep"))
        ++m_keptCount;
    else
        ++m_rejectedCount;
    // Retain the already-decoded image (and whether it was full quality) so
    // undo can restore it instantly, skipping the synchronous re-decode that
    // used to freeze the UI.
    const CachedImage cached = m_preloaded.value(fi.absoluteFilePath());
    actionInfo.image = cached.image;
    actionInfo.imageFull = cached.full;
    m_undoStack.push_back(actionInfo);
    if (static_cast<int>(m_undoStack.size()) > MAX_UNDO) {
        m_undoStack.pop_front();
    }
    // Bound memory: keep decoded pixels only for the most recent moves. Deeper
    // undos fall back to a fast async load via displayCurrentImage().
    for (int i = 0; i < static_cast<int>(m_undoStack.size()) - UNDO_IMAGE_RETAIN; ++i) {
        m_undoStack[i].image = QImage();
    }
    // Remove from list
    int removedIndex = m_currentIndex;
    m_images.erase(m_images.begin() + removedIndex);
    // Adjust index to show next image
    if (m_currentIndex >= static_cast<int>(m_images.size())) {
        m_currentIndex = static_cast<int>(m_images.size()) - 1;
    }
    // Remove the cache entry for the file that is being removed
    const QString removedKey = fi.absoluteFilePath();
    m_preloaded.remove(removedKey);
    m_loading.remove(removedKey);
    m_fullRequested.remove(removedKey);
    // Keep the thumbnail cached: the file content is unchanged, so if this
    // image is restored via undo the list row shows its real icon immediately.
    // Update the file list widget: remove the corresponding item instead of
    // rebuilding the entire list.  This keeps UI interactions snappy by
    // avoiding unnecessary iterations.  Guard against null pointer just in case.

    // if (m_fileListWidget) {
    //     QListWidgetItem *item = m_fileListWidget->takeItem(removedIndex);
    //     delete item;
    //     // Select the new current index after removal
    //     if (m_currentIndex >= 0) {

    //         m_fileListWidget->setCurrentRow(m_currentIndex);

    //     }
    //     m_fileListWidget->setCurrentRow(m_currentIndex);
    // }
    if (m_fileListWidget) {
            m_fileListWidget->blockSignals(true);
            QListWidgetItem *item = m_fileListWidget->takeItem(removedIndex);
            delete item;
            if (m_currentIndex >= 0) {
                    m_fileListWidget->setCurrentRow(m_currentIndex);
                }
            m_fileListWidget->blockSignals(false);
        }


    displayCurrentImage();
    ensurePreloadWindow();
    // Remove the removed key from the set of currently loading thumbnails if
    // present.  We do not clear the entire loading state because other
    // thumbnails may still be loading and can continue uninterrupted.  When
    // their loads finish they will be ignored if their path no longer exists
    // in m_images.
    m_thumbLoadingPaths.remove(removedKey);
    // Queue loading of thumbnails for any images that now lack previews.
    startThumbnailLoaders();
    // preloadNext();
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
    // Reverse the kept/rejected tally for this action.
    if (action.kind == QLatin1String("keep"))
        m_keptCount = qMax(0, m_keptCount - 1);
    else
        m_rejectedCount = qMax(0, m_rejectedCount - 1);
    // Undo the move: if the move has not yet been processed by the
    // background worker, cancel the pending task.  Otherwise move
    // the file back from its destination to the original location.
    bool originalExists = QFileInfo::exists(action.originalPath);
    if (originalExists) {
        // File is still at original location; cancel the queued move
        if (m_fileWorker) {
            m_fileWorker->cancelTask(action.originalPath);
        }
    } else {
        // File has been moved; perform the reverse move
        QDir origDir = QFileInfo(action.originalPath).absoluteDir();
        if (!origDir.exists()) {
            origDir.mkpath(".");
        }
        if (!QFile::rename(action.destinationPath, action.originalPath)) {
            QMessageBox::critical(this, tr("Error Undoing File Move"), tr("Could not restore %1 to %2").arg(action.destinationPath, action.originalPath));
            return;
        }
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
    // Re-seed the decoded image (if still retained) so the restored photo
    // paints instantly with no synchronous decode. Key by absolute path to
    // match the lookup in displayCurrentImage(). If we no longer hold the
    // pixels, drop any stale entry and let displayCurrentImage() load it
    // asynchronously (placeholder first, no UI freeze).
    const QString restoredKey = m_images.at(m_currentIndex).absoluteFilePath();
    if (!action.image.isNull())
        m_preloaded.insert(restoredKey, CachedImage{action.image, action.imageFull});
    else
        m_preloaded.remove(restoredKey);
    // Clear any stale in-flight markers so displayCurrentImage() can re-request
    // a full-quality decode if the retained copy was only a fast RAW preview.
    m_loading.remove(restoredKey);
    m_fullRequested.remove(restoredKey);
    // The thumbnail is kept (file content is unchanged) so the restored row
    // can show its real icon immediately rather than a generic placeholder.
    // Insert the restored entry into the file list widget instead of rebuilding all items.
    if (m_fileListWidget) {
        static QFileIconProvider iconProvider;
        QListWidgetItem *newItem = new QListWidgetItem();
        newItem->setText(m_showNames ? QFileInfo(action.originalPath).fileName() : QString());
        const QString opath = restoredKey;   // absolute path, matches cache keys
        if (m_thumbnailCache.contains(opath)) {
            newItem->setIcon(QIcon(m_thumbnailCache.value(opath)));
        } else {
            newItem->setIcon(iconProvider.icon(QFileInfo(opath)));
        }
        m_fileListWidget->insertItem(insertIndex, newItem);
        // Select the newly restored item
        m_fileListWidget->blockSignals(true);
        m_fileListWidget->setCurrentRow(m_currentIndex);
        m_fileListWidget->blockSignals(false);
    }
    displayCurrentImage();
    ensurePreloadWindow();
    // Remove the restored file path from the loading set in case a load is
    // still pending for this item.  We avoid clearing the entire loading
    // state to preserve in‑flight thumbnail loads for other images.  Those
    // loads will update icons correctly once they complete.
    m_thumbLoadingPaths.remove(action.originalPath);
    // Queue loading of any thumbnails that are still missing.  This will
    // schedule the restored item for loading if needed.
    startThumbnailLoaders();
    // preloadNext();
}

// Move to the next image in the list without making any changes.  If already
// at the last image, the index will remain unchanged.  This slot is triggered
// by the Right arrow key.
void PhotoTriageWindow::goToNextImage()
{
    if (m_currentIndex >= 0 && m_currentIndex + 1 < static_cast<int>(m_images.size())) {
        m_currentIndex++;
        displayCurrentImage();
        ensurePreloadWindow();
    }
}

// Move to the previous image in the list without making any changes.  If
// already at the first image, the index will remain unchanged.  This slot is
// triggered by the Left arrow key.
void PhotoTriageWindow::goToPreviousImage()
{
    if (m_currentIndex > 0) {
        m_currentIndex--;
        displayCurrentImage();
        ensurePreloadWindow();
    }
}

// Respond to changes in the file list selection.  Updating m_currentIndex
// allows the central image view to display the newly selected photo.  This
// function is connected to the QListWidget::currentRowChanged signal.
void PhotoTriageWindow::onFileListSelectionChanged(int row)
{
    if (row < 0 || row >= static_cast<int>(m_images.size()))
        return;
    // Update current index and display the selected image
    m_currentIndex = row;
    displayCurrentImage();
    ensurePreloadWindow();
}

// Build or rebuild the file browser list.  Each entry displays a thumbnail
// preview alongside the filename.  Thumbnails are generated synchronously
// using scaled QPixmaps; because they are small (80×80) and we avoid loading
// full-size pixmaps where possible, the performance impact remains acceptable
// for modest numbers of images.  If images cannot be loaded, a placeholder
// icon is used.
void PhotoTriageWindow::populateFileList()
{
    if (!m_fileListWidget)
        return;
    m_fileListWidget->clear();
    const int count = static_cast<int>(m_images.size());
    // QFileIconProvider caches icons per file type and avoids costly image decoding.  Using
    // it here keeps list population lightweight and preserves the responsive caching and
    // preloading behaviour of the main image view.
    static QFileIconProvider iconProvider;
    m_fileListWidget->setUpdatesEnabled(false);
    for (int i = 0; i < count; ++i) {
        const QFileInfo &fi = m_images.at(i);
        QListWidgetItem *item = new QListWidgetItem();
        item->setText(m_showNames ? fi.fileName() : QString());
        // If a cached thumbnail exists, use it; otherwise use a generic file icon
        const QString path = fi.absoluteFilePath();
        if (m_thumbnailCache.contains(path)) {
            item->setIcon(QIcon(m_thumbnailCache.value(path)));
        } else {
            item->setIcon(iconProvider.icon(fi));
        }
        m_fileListWidget->addItem(item);
    }
    m_fileListWidget->setUpdatesEnabled(true);
    // Set the current selection and scroll into view without emitting signals
    if (m_currentIndex >= 0 && m_currentIndex < count) {
        m_fileListWidget->blockSignals(true);
        m_fileListWidget->setCurrentRow(m_currentIndex);
        m_fileListWidget->scrollToItem(m_fileListWidget->currentItem(), QAbstractItemView::PositionAtCenter);
        m_fileListWidget->blockSignals(false);
    }

    // After building the list, start loading thumbnails asynchronously.  This
    // call will skip items that already have cached previews or that are
    // currently being loaded.
    startThumbnailLoaders();
}
