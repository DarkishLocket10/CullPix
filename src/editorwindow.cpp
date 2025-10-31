// editorwindow.cpp
//
// Implements the EditorWindow class.  This dialog provides slider
// controls for exposure, contrast and highlight recovery.  As the
// user adjusts the sliders, a live preview is rendered.  Upon
// acceptance, the full‑resolution image is processed and returned
// through the editedImage() accessor.

#include "editorwindow.h"
#include "imageadjust.h"

#include <QLabel>
#include <QSlider>
#include <QPushButton>
#include <QBoxLayout>
#include <QDialogButtonBox>
#include <QImage>
#include <QPixmap>
#include <QStyle>
#include <QResizeEvent>
#include <QTimer>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>

EditorWindow::EditorWindow(const QImage &image, QWidget *parent)
    : QDialog(parent),
      m_original(image),
      m_previewBase(image)
{
    // Set up the window
    setWindowTitle(tr("Edit Image"));
    // Ensure a reasonable minimum size; user can resize as needed.
    setMinimumSize(500, 400);
    resize(900, 700);
    setSizeGripEnabled(true);  // Enable resize grip for easier resizing
    
    // Copy the original image for editing.  If invalid the dialog will
    // simply show a placeholder.
    if (!m_original.isNull()) {
        // Precompute a scaled version for preview while keeping aspect ratio.
        // Use a reasonable max size that balances quality and performance.
        const int maxPreviewWidth  = 1920;
        const int maxPreviewHeight = 1080;
        m_previewBase = m_original.scaled(maxPreviewWidth,
                                          maxPreviewHeight,
                                          Qt::KeepAspectRatio,
                                          Qt::SmoothTransformation);
    }

    // Create UI elements
    m_previewLabel = new QLabel(this);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_previewLabel->setMinimumSize(200, 150);  // Smaller minimum to allow window resizing
    m_previewLabel->setScaledContents(false);  // We handle scaling manually
    m_previewLabel->setStyleSheet("background-color: #111111; border: 1px solid #444444;");

    // Sliders: use ranges that map to meaningful parameter ranges.
    // Exposure: -50..50 corresponds to -5 EV..+5 EV (0.1 EV per tick)
    m_exposureSlider = new QSlider(Qt::Horizontal, this);
    m_exposureSlider->setRange(-50, 50);
    m_exposureSlider->setValue(0);
    m_exposureSlider->setTickPosition(QSlider::TicksBelow);
    m_exposureSlider->setToolTip(tr("Exposure (EV): -5..+5"));

    // Contrast: 0..200 corresponds to 0..2x contrast (100 = neutral)
    m_contrastSlider = new QSlider(Qt::Horizontal, this);
    m_contrastSlider->setRange(0, 200);
    m_contrastSlider->setValue(100);
    m_contrastSlider->setTickPosition(QSlider::TicksBelow);
    m_contrastSlider->setToolTip(tr("Contrast: 0..2"));

    // Highlight recovery: 0..100 (0 = none, 100 = strong compression)
    m_highlightSlider = new QSlider(Qt::Horizontal, this);
    m_highlightSlider->setRange(0, 100);
    m_highlightSlider->setValue(0);
    m_highlightSlider->setTickPosition(QSlider::TicksBelow);
    m_highlightSlider->setToolTip(tr("Highlight Recovery: 0..1"));

    // Buttons
    m_okButton = new QPushButton(tr("Apply"), this);
    m_saveAsButton = new QPushButton(tr("Save As..."), this);
    m_cancelButton = new QPushButton(tr("Cancel"), this);
    // Connect signals
    connect(m_exposureSlider, &QSlider::valueChanged, this, &EditorWindow::updatePreview);
    connect(m_contrastSlider, &QSlider::valueChanged, this, &EditorWindow::updatePreview);
    connect(m_highlightSlider, &QSlider::valueChanged, this, &EditorWindow::updatePreview);
    connect(m_okButton, &QPushButton::clicked, this, &EditorWindow::onAccept);
    connect(m_saveAsButton, &QPushButton::clicked, this, &EditorWindow::onSaveAs);
    connect(m_cancelButton, &QPushButton::clicked, this, &EditorWindow::reject);

    // Set up resize timer for smooth scaling after resize completes
    m_resizeTimer = new QTimer(this);
    m_resizeTimer->setSingleShot(true);
    m_resizeTimer->setInterval(150);  // Wait 150ms after last resize event
    connect(m_resizeTimer, &QTimer::timeout, this, &EditorWindow::onResizeFinished);

    // Layout the controls.  Use a vertical layout for the preview on top
    // and a horizontal box for sliders beneath.
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(m_previewLabel, /*stretch=*/1);
    // Sliders layout: each slider with a label above it
    QVBoxLayout *slidersLayout = new QVBoxLayout();
    // Exposure
    QLabel *expLabel = new QLabel(tr("Exposure"), this);
    expLabel->setAlignment(Qt::AlignLeft);
    slidersLayout->addWidget(expLabel);
    slidersLayout->addWidget(m_exposureSlider);
    // Contrast
    QLabel *contrLabel = new QLabel(tr("Contrast"), this);
    contrLabel->setAlignment(Qt::AlignLeft);
    slidersLayout->addWidget(contrLabel);
    slidersLayout->addWidget(m_contrastSlider);
    // Highlight
    QLabel *hiLabel = new QLabel(tr("Highlights"), this);
    hiLabel->setAlignment(Qt::AlignLeft);
    slidersLayout->addWidget(hiLabel);
    slidersLayout->addWidget(m_highlightSlider);
    mainLayout->addLayout(slidersLayout);
    // Buttons at the bottom right
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_saveAsButton);
    buttonLayout->addWidget(m_okButton);
    buttonLayout->addWidget(m_cancelButton);
    mainLayout->addLayout(buttonLayout);
    setLayout(mainLayout);

    // Initial preview
    updatePreview();
}

EditorWindow::~EditorWindow() = default;

QImage EditorWindow::editedImage() const
{
    if (!m_final.isNull())
        return m_final;
    return m_original;
}

void EditorWindow::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    // Use fast scaling during active resizing for responsiveness
    updateDisplayedPixmap(true);
    // Restart the timer - smooth scaling will be applied after resizing stops
    m_resizeTimer->start();
}

void EditorWindow::updatePreview()
{
    // Recompute adjustments when slider values change
    computeAdjustedPreview();
    // Update the displayed pixmap
    updateDisplayedPixmap();
}

void EditorWindow::onAccept()
{
    computeFullEdit();
    accept();
}

void EditorWindow::onResizeFinished()
{
    // Apply smooth scaling now that resizing has finished
    updateDisplayedPixmap(false);
}

void EditorWindow::onSaveAs()
{
    // Compute the full resolution edited image with current adjustments
    computeFullEdit();
    
    if (m_final.isNull()) {
        QMessageBox::warning(this, tr("Save As"), 
                           tr("Cannot save: no valid image to save."));
        return;
    }
    
    // Open file dialog to choose save location
    QString filter = tr("JPEG Images (*.jpg *.jpeg);;PNG Images (*.png);;TIFF Images (*.tiff *.tif);;All Files (*)");
    QString fileName = QFileDialog::getSaveFileName(this,
                                                    tr("Save Edited Image"),
                                                    QString(),  // Default directory
                                                    filter);
    
    if (fileName.isEmpty()) {
        return;  // User cancelled
    }
    
    // Determine format from file extension
    QFileInfo fileInfo(fileName);
    QString suffix = fileInfo.suffix().toLower();
    const char* format = nullptr;
    int quality = -1;  // -1 uses default quality
    
    if (suffix == "jpg" || suffix == "jpeg") {
        format = "JPEG";
        quality = 95;  // High quality JPEG
    } else if (suffix == "png") {
        format = "PNG";
    } else if (suffix == "tiff" || suffix == "tif") {
        format = "TIFF";
    } else {
        // Try to auto-detect or use PNG as fallback
        format = "PNG";
        if (!fileName.endsWith(".png", Qt::CaseInsensitive)) {
            fileName += ".png";
        }
    }
    
    // Save the image
    bool success = m_final.save(fileName, format, quality);
    
    if (success) {
        QMessageBox::information(this, tr("Save As"),
                               tr("Image saved successfully to:\n%1").arg(fileName));
    } else {
        QMessageBox::critical(this, tr("Save As"),
                            tr("Failed to save image to:\n%1").arg(fileName));
    }
}

void EditorWindow::computeAdjustedPreview()
{
    if (m_previewBase.isNull()) {
        m_adjustedPreview = QImage();
        return;
    }
    
    // Map slider values to parameters
    float exposureEV = m_exposureSlider->value() / 10.f; // -5..+5
    float contrastFactor = m_contrastSlider->value() / 100.f; // 0..2
    float highlightFactor = m_highlightSlider->value() / 100.f; // 0..1
    
    // Apply adjustments to the preview base image and cache the result
    m_adjustedPreview = ImageAdjust::applyAdjustments(m_previewBase, 
                                                       exposureEV, 
                                                       contrastFactor, 
                                                       highlightFactor);
}

void EditorWindow::updateDisplayedPixmap(bool useFastScaling)
{
    // Choose the transformation mode based on whether we're actively resizing
    Qt::TransformationMode transformMode = useFastScaling 
        ? Qt::FastTransformation 
        : Qt::SmoothTransformation;
    
    if (m_adjustedPreview.isNull()) {
        if (m_previewBase.isNull()) {
            // Nothing to show
            m_previewLabel->clear();
            m_previewLabel->setText(tr("No preview available."));
        } else {
            // Show the base image if adjustments haven't been computed yet
            QSize labelSize = m_previewLabel->size();
            QPixmap pix = QPixmap::fromImage(m_previewBase);
            // Always scale to fit the label
            pix = pix.scaled(labelSize, Qt::KeepAspectRatio, transformMode);
            m_previewLabel->setPixmap(pix);
        }
        return;
    }
    
    // Always scale the adjusted preview to fit the label while maintaining aspect ratio.
    // This ensures the pixmap never dictates the window size.
    QSize labelSize = m_previewLabel->size();
    
    // Ensure we have a valid size before scaling
    if (labelSize.width() < 1 || labelSize.height() < 1) {
        return;
    }
    
    QPixmap pix = QPixmap::fromImage(m_adjustedPreview);
    // Always scale to fit - this is crucial for allowing window resizing
    pix = pix.scaled(labelSize, Qt::KeepAspectRatio, transformMode);
    m_previewLabel->setPixmap(pix);
}

void EditorWindow::computeFullEdit()
{
    if (m_original.isNull()) {
        m_final = m_original;
        return;
    }
    float exposureEV = m_exposureSlider->value() / 10.f;
    float contrastFactor = m_contrastSlider->value() / 100.f;
    float highlightFactor = m_highlightSlider->value() / 100.f;
    m_final = ImageAdjust::applyAdjustments(m_original, exposureEV, contrastFactor, highlightFactor);
}
