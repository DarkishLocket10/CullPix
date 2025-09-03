// rawloader.cpp
#include "rawloader.h"
#include <libraw/libraw.h>
#include <QImage>
#include <QByteArray>

static QImage qimageFromMemImage(const libraw_processed_image_t* img)
{
    if (!img) return {};
    if (img->type == LIBRAW_IMAGE_BITMAP) {
        // img->colors=3, img->bits=8 expected
        const int w = img->width, h = img->height;
        const uchar* data = img->data;
        // LibRaw bitmap is 8-bit RGB
        QImage out(w, h, QImage::Format_RGB888);
        if (out.isNull()) return {};
        memcpy(out.bits(), data, size_t(w)*h*3);
        return out;
    } else if (img->type == LIBRAW_IMAGE_JPEG) {
        // Decode JPEG buffer to QImage
        QByteArray ba(reinterpret_cast<const char*>(img->data), int(img->data_size));
        QImage out;
        out.loadFromData(ba, "JPG");
        return out;
    }
    return {};
}

bool RawLoader::loadEmbeddedPreview(const QString& path, QImage& out)
{
    LibRaw raw;
    if (raw.open_file(path.toLocal8Bit().constData()) != LIBRAW_SUCCESS)
        return false;

    if (raw.unpack_thumb() != LIBRAW_SUCCESS)
        return false;

    const libraw_processed_image_t* pi = raw.dcraw_make_mem_thumb();
    if (!pi) return false;

    QImage img = qimageFromMemImage(pi);
    raw.dcraw_clear_mem(const_cast<libraw_processed_image_t*>(pi));
    if (img.isNull()) return false;

    // Apply orientation if needed
    int rot = raw.imgdata.sizes.flip; // 0,3,5,6… see LibRaw docs
    if (rot == 3) img = img.transformed(QTransform().rotate(180));
    else if (rot == 6) img = img.transformed(QTransform().rotate(90));
    else if (rot == 8) img = img.transformed(QTransform().rotate(270));

    out = std::move(img);
    return true;
}

bool RawLoader::loadDemosaiced(const QString& path, QImage& out, bool halfSize)
{
    LibRaw raw;
    if (raw.open_file(path.toLocal8Bit().constData()) != LIBRAW_SUCCESS)
        return false;

    // Unpack RAW data
    if (raw.unpack() != LIBRAW_SUCCESS)
        return false;

    // Postprocess params: make something pleasant for screen
    raw.imgdata.params.use_auto_wb   = 1;
    raw.imgdata.params.no_auto_bright = 1; // avoid blown highlights
    raw.imgdata.params.output_bps    = 8;  // 8-bit output (fast)
    raw.imgdata.params.output_color  = 1;  // sRGB
    raw.imgdata.params.half_size     = halfSize ? 1 : 0; // speed win!

    if (raw.dcraw_process() != LIBRAW_SUCCESS)
        return false;

    const libraw_processed_image_t* pi = raw.dcraw_make_mem_image();
    if (!pi) return false;

    QImage img = qimageFromMemImage(pi);
    raw.dcraw_clear_mem(const_cast<libraw_processed_image_t*>(pi));
    if (img.isNull()) return false;

    // Orientation (if any left after processing)
    int rot = raw.imgdata.sizes.flip;
    if (rot == 3) img = img.transformed(QTransform().rotate(180));
    else if (rot == 6) img = img.transformed(QTransform().rotate(90));
    else if (rot == 8) img = img.transformed(QTransform().rotate(270));

    out = std::move(img);
    return true;
}
