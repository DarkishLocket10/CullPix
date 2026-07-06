// imageview.cpp

#include "imageview.h"

#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QLabel>
#include <QImage>
#include <QPixmap>
#include <QPainter>
#include <QColor>
#include <QWheelEvent>
#include <QNativeGestureEvent>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QScrollBar>

ImageView::ImageView(QWidget *parent)
    : QGraphicsView(parent)
{
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);

    setFrameShape(QFrame::NoFrame);
    setAlignment(Qt::AlignCenter);
    // Pan is handled manually (mousePress/Move/Release) rather than via
    // ScrollHandDrag: that mode forces a grab cursor across the whole view up
    // to the window edge, which suppresses the OS resize cursor there. With
    // NoDrag the view keeps a normal cursor on hover (so edges show the resize
    // cursor) and shows the grab cursor only while actively dragging.
    setDragMode(QGraphicsView::NoDrag);
    // We anchor zoom manually (see zoomBy) off each event's cursor position,
    // so no transformation anchor is needed; resizing keeps the view centered.
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    viewport()->setMouseTracking(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setRenderHint(QPainter::SmoothPixmapTransform, true);
    setBackgroundBrush(QColor("#111111"));
    setMinimumSize(1, 1);   // never gate the window's minimum size

    m_overlay = new QLabel(viewport());
    m_overlay->setAlignment(Qt::AlignCenter);
    m_overlay->setStyleSheet("color:#E0E0E0; background-color:#111111;");
    m_overlay->hide();
}

qreal ImageView::deviceScale() const
{
    return devicePixelRatioF();
}

qreal ImageView::fitScale() const
{
    if (!m_item)
        return 1.0;
    const QSizeF img = m_item->boundingRect().size();
    const QSize vp = viewport()->size();
    if (img.width() <= 0.0 || img.height() <= 0.0)
        return 1.0;
    return qMin(vp.width() / img.width(), vp.height() / img.height());
}

void ImageView::setImage(const QImage &image)
{
    if (image.isNull()) {
        showMessage(tr("Unable to load image"));
        return;
    }
    QPixmap pm = QPixmap::fromImage(image);
    pm.setDevicePixelRatio(1.0);   // scene units == raw image pixels
    if (!m_item) {
        m_item = m_scene->addPixmap(pm);
    } else {
        m_item->setPixmap(pm);
    }
    m_scene->setSceneRect(m_item->boundingRect());

    m_overlay->hide();
    fitToWindow();                 // each new image starts fit-to-window
}

void ImageView::showMessage(const QString &text)
{
    m_overlay->setText(text);
    m_overlay->setGeometry(viewport()->rect());
    m_overlay->show();
    m_overlay->raise();
    if (m_cornerWidget)            // keep the gear reachable over the message
        m_cornerWidget->raise();
}

void ImageView::setCornerWidget(QWidget *w)
{
    m_cornerWidget = w;
    if (w) {
        w->setParent(viewport());
        positionCornerWidget();
        w->show();
        w->raise();
    }
}

void ImageView::positionCornerWidget()
{
    if (m_cornerWidget)
        m_cornerWidget->move(viewport()->width() - m_cornerWidget->width() - 12, 12);
}

void ImageView::setActive(bool on)
{
    if (m_active == on)
        return;
    m_active = on;
    // The ring is painted at fixed viewport coordinates, which the default
    // minimal update mode would smear across the photo when scrolling (it
    // blits the viewport and repaints only the exposed strips). Repaint the
    // full viewport while the ring is up; restore the cheaper mode when not.
    setViewportUpdateMode(on ? QGraphicsView::FullViewportUpdate
                             : QGraphicsView::MinimalViewportUpdate);
    viewport()->update();   // repaint the active border
}

void ImageView::drawForeground(QPainter *painter, const QRectF &rect)
{
    QGraphicsView::drawForeground(painter, rect);
    if (!m_active)
        return;
    // Gentle accent ring around the active pane, drawn in viewport coordinates.
    painter->save();
    painter->setWorldMatrixEnabled(false);
    painter->setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor("#2A9D8F"), 3);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(QRectF(viewport()->rect()).adjusted(1.5, 1.5, -1.5, -1.5));
    painter->restore();
}

void ImageView::fitToWindow()
{
    m_fitMode = true;
    if (m_item) {
        fitInView(m_item, Qt::KeepAspectRatio);
        updateSmoothing();
    }
}

void ImageView::zoomTo100()
{
    if (!m_item)
        return;
    m_fitMode = false;
    resetTransform();
    const qreal s = 1.0 / deviceScale();   // 1 image px per *physical* px
    scale(s, s);
    centerOn(m_item);
    updateSmoothing();
}

void ImageView::zoomBy(qreal factor, const QPointF &anchorViewPos)
{
    if (!m_item)
        return;
    const qreal cur = transform().m11();
    const qreal fit = fitScale();
    const qreal maxScale = 8.0 / deviceScale();   // up to 800% actual pixels
    const qreal lo = qMin(fit, maxScale);
    const qreal hi = qMax(fit, maxScale);
    const qreal target = qBound(lo, cur * factor, hi);
    if (qFuzzyCompare(target, cur))
        return;

    // Zoom about the cursor: note the scene point currently under it, scale,
    // then scroll so that same scene point lands back under the cursor. Driven
    // by the event's own position, so it works on hover with no prior click.
    const QPointF sceneAnchor = mapToScene(anchorViewPos.toPoint());
    scale(target / cur, target / cur);
    const QPointF drift = QPointF(mapFromScene(sceneAnchor)) - anchorViewPos;
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() + qRound(drift.x()));
    verticalScrollBar()->setValue(verticalScrollBar()->value() + qRound(drift.y()));

    m_fitMode = (target <= fit * 1.0001);         // snapped back to fit
    updateSmoothing();
}

void ImageView::updateSmoothing()
{
    if (!m_item)
        return;
    // Effective resolution = device pixels per image pixel. At or above 1:1 we
    // want nearest-neighbour so the user inspects real pixels, not interpolation.
    const qreal effective = transform().m11() * deviceScale();
    m_item->setTransformationMode(effective >= 1.0 ? Qt::FastTransformation
                                                   : Qt::SmoothTransformation);
}

void ImageView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    if (m_overlay)
        m_overlay->setGeometry(viewport()->rect());
    positionCornerWidget();
    if (m_fitMode)
        fitToWindow();
}

void ImageView::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier)) {
        const int dy = event->angleDelta().y();
        if (dy != 0)
            zoomBy(dy > 0 ? 1.15 : 1.0 / 1.15, event->position());
        event->accept();
    } else {
        QGraphicsView::wheelEvent(event);   // two-finger scroll pans when zoomed
    }
}

bool ImageView::viewportEvent(QEvent *event)
{
    if (event->type() == QEvent::NativeGesture) {
        auto *g = static_cast<QNativeGestureEvent *>(event);
        if (g->gestureType() == Qt::ZoomNativeGesture) {
            zoomBy(1.0 + g->value(), g->position());   // trackpad pinch
            return true;
        }
    }
    return QGraphicsView::viewportEvent(event);
}

void ImageView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (m_fitMode) {
        // Zoom to 100% anchored on the clicked point, not the image center.
        const qreal cur = transform().m11();
        const qreal target = 1.0 / deviceScale();   // 100% actual pixels
        if (cur > 0.0)
            zoomBy(target / cur, event->position());
    } else {
        fitToWindow();
    }
    event->accept();
}

void ImageView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        emit activated();   // clicking a pane makes it the active one (compare)
    // Begin a drag-to-pan. The grab cursor is shown only now (not on hover),
    // so a plain hover keeps the normal cursor and the OS resize cursor can
    // appear at the window edges.
    if (event->button() == Qt::LeftButton && m_item) {
        m_panning = true;
        m_panLast = event->position().toPoint();
        viewport()->setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void ImageView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_panning) {
        const QPoint p = event->position().toPoint();
        const QPoint d = p - m_panLast;
        m_panLast = p;
        // No-op at fit (scrollbars have no range); pans once zoomed in.
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - d.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - d.y());
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void ImageView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_panning && event->button() == Qt::LeftButton) {
        m_panning = false;
        viewport()->unsetCursor();
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void ImageView::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_0: fitToWindow(); event->accept(); return;
    case Qt::Key_1: zoomTo100();   event->accept(); return;
    case Qt::Key_Plus:
    case Qt::Key_Equal:  zoomBy(1.25, viewport()->rect().center());       event->accept(); return;
    case Qt::Key_Minus:  zoomBy(1.0 / 1.25, viewport()->rect().center()); event->accept(); return;
    default: break;
    }
    QGraphicsView::keyPressEvent(event);
}
