// imageview.h
//
// A zoomable, pannable image view built on QGraphicsView. It renders at the
// display's physical pixels (so fit-to-window is full quality on high-DPI
// screens) and supports trackpad pinch, Cmd/Ctrl+wheel, and double-click to
// toggle between fit-to-window and 1:1 — where one image pixel maps to one
// physical device pixel, i.e. actual sensor pixels with no resampling.
#pragma once

#include <QGraphicsView>
#include <QString>

class QGraphicsScene;
class QGraphicsPixmapItem;
class QLabel;
class QImage;

class ImageView : public QGraphicsView
{
    Q_OBJECT
public:
    explicit ImageView(QWidget *parent = nullptr);

    // Show a full-quality image. Resets the view to fit-to-window.
    void setImage(const QImage &image);
    // Show centered text (e.g. "Loading…", "No images.") over a blank
    // background, covering any previous image.
    void showMessage(const QString &text);

public slots:
    void fitToWindow();   // scale so the whole image is visible
    void zoomTo100();     // 1:1 — one image pixel per physical device pixel

protected:
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    bool viewportEvent(QEvent *event) override;            // trackpad pinch
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;     // start drag-to-pan
    void mouseMoveEvent(QMouseEvent *event) override;      // pan while dragging
    void mouseReleaseEvent(QMouseEvent *event) override;   // end drag-to-pan
    void keyPressEvent(QKeyEvent *event) override;

private:
    qreal deviceScale() const;     // physical pixels per logical pixel (dpr)
    qreal fitScale() const;        // view scale that fits the whole image
    // Multiply the current zoom (clamped), keeping the scene point under
    // anchorViewPos fixed on screen — i.e. zoom about the cursor.
    void  zoomBy(qreal factor, const QPointF &anchorViewPos);
    void  updateSmoothing();       // smooth when downscaling, nearest at >=1:1

    QGraphicsScene      *m_scene = nullptr;
    QGraphicsPixmapItem *m_item = nullptr;
    QLabel              *m_overlay = nullptr;   // centered message text
    bool                 m_fitMode = true;
    bool                 m_panning = false;     // mid drag-to-pan
    QPoint               m_panLast;             // last cursor pos while panning
};
