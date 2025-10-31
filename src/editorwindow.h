// editorwindow.h
//
// Declares the EditorWindow class which provides a simple dialog for
// adjusting exposure, contrast and highlight recovery on an image.  The
// window displays a live preview of the adjustments and returns the
// edited image when accepted.  This dialog is intended for use from
// PhotoTriageWindow to offer basic editing functionality similar to
// fundamental controls in raw converters.

#pragma once

#include <QDialog>
#include <QImage>

class QLabel;
class QSlider;
class QPushButton;
class QTimer;

class EditorWindow : public QDialog
{
    Q_OBJECT
public:
    // Construct an editor for the given image.  The original image is
    // copied internally and never modified.  The parent may be null.
    explicit EditorWindow(const QImage &image, QWidget *parent = nullptr);
    ~EditorWindow() override;

    // Return the edited image after the dialog has been accepted.  If
    // the dialog was cancelled this returns the original.
    QImage editedImage() const;

protected:
    // Override to update the preview when the window is resized.
    void resizeEvent(QResizeEvent *event) override;

private slots:
    // Recalculate the preview when any slider changes.
    void updatePreview();
    // Accept the dialog and compute the final edited image at full
    // resolution.  This slot is connected to the OK button.
    void onAccept();
    // Apply smooth scaling after resizing has finished.
    void onResizeFinished();
    // Save the edited image to a new file chosen by the user.
    void onSaveAs();

private:
    // Helper to apply adjustments to the preview base and cache the result.
    // This is called when slider values change.
    void computeAdjustedPreview();
    // Helper to update the displayed pixmap from the cached adjusted preview.
    // This is called on resize or after adjustment computation.
    // The useFastScaling parameter controls whether to use fast or smooth
    // transformation for the scaling operation.
    void updateDisplayedPixmap(bool useFastScaling = false);
    // Helper to compute the full resolution edited image using the
    // current slider values.  Called on accept.
    void computeFullEdit();

    // Original full‑resolution image.
    QImage m_original;
    // Scaled version of the original used for the preview.  This is
    // recomputed only when the editor is constructed; the
    // preview adjustments are applied on top of this.
    QImage m_previewBase;
    // Cached adjusted preview image (adjustments applied to m_previewBase).
    // This is recomputed when slider values change, but not on resize.
    QImage m_adjustedPreview;
    // The final edited image computed on acceptance.  If the dialog
    // is rejected this remains empty and the original will be used.
    QImage m_final;
    // UI elements
    QLabel *m_previewLabel;
    QSlider *m_exposureSlider;
    QSlider *m_contrastSlider;
    QSlider *m_highlightSlider;
    QPushButton *m_okButton;
    QPushButton *m_saveAsButton;
    QPushButton *m_cancelButton;
    // Timer to detect when resizing has finished
    QTimer *m_resizeTimer;
};
