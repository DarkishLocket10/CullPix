// rawloader.cpp
#include "rawloader.h"
#include <libraw/libraw.h>
#include <QImage>
#include <QFileInfo>

static inline QByteArray toBa(const QString& s){ return s.toLocal8Bit(); }

bool RawLoader::isRawExtension(const QString& p)
{
    const QString ext = QFileInfo(p).suffix().toLower();
    // Extend as needed:
    static const QStringList raws = { "arw","cr2","cr3","nef","raf","rw2","orf","srw","dng","pef","3fr","erf","sr2" };
    return raws.contains(ext);
}

QImage RawLoader::loadPreviewOrHalf(const QString& path, bool allowHalfDemosaic)
{
    LibRaw raw;
    if (raw.open_file(toBa(path).constData()) != LIBRAW_SUCCESS)
        return QImage();

    // 1) Try embedded preview (usually a large/full-res JPEG, very fast)
    if (raw.unpack_thumb() == LIBRAW_SUCCESS) {
        libraw_processed_image_t *thumb = nullptr;
        int err = 0;
        thumb = raw.dcraw_make_mem_thumb(&err);
        if (thumb && err == LIBRAW_SUCCESS && thumb->type == LIBRAW_IMAGE_JPEG) {
            QImage img;
            img.loadFromData(thumb->data, thumb->data_size, "JPG");
            raw.dcraw_clear_mem(thumb);
            raw.recycle();
            return img;                       // done!
        }
        if (thumb) raw.dcraw_clear_mem(thumb);
    }

    if (!allowHalfDemosaic) { raw.recycle(); return QImage(); }

    // 2) Fallback: quick demosaic (half-size for speed)
    if (raw.unpack() != LIBRAW_SUCCESS) { raw.recycle(); return QImage(); }

    auto *params = raw.output_params_ptr();
    params->half_size     = 1;   // half-res for speed
    params->use_camera_wb = 1;   // camera WB feels more like the embedded preview
    params->no_auto_bright= 1;   // avoid candy-looking auto-bright

    if (raw.dcraw_process() != LIBRAW_SUCCESS) { raw.recycle(); return QImage(); }

    int err = 0;
    libraw_processed_image_t *rgb = raw.dcraw_make_mem_image(&err);
    if (!rgb || err != LIBRAW_SUCCESS) { raw.recycle(); return QImage(); }

    QImage out;
    if (rgb->type == LIBRAW_IMAGE_BITMAP) {
        if (rgb->bits == 8 && rgb->colors == 3) {
            // LibRaw is RGB; QImage expects RGB888 (not premultiplied)
            out = QImage(rgb->data, rgb->width, rgb->height, rgb->width*3, QImage::Format_RGB888).copy();
        } else if (rgb->bits == 16 && rgb->colors == 3) {
            // Convert 16-bit to 8-bit for display
            out = QImage(rgb->width, rgb->height, QImage::Format_RGB888);
            const uint16_t *src = reinterpret_cast<const uint16_t*>(rgb->data);
            for (int y=0; y<rgb->height; ++y) {
                uchar *dst = out.scanLine(y);
                for (int x=0; x<rgb->width; ++x) {
                    *dst++ = static_cast<uchar>(src[0] >> 8);
                    *dst++ = static_cast<uchar>(src[1] >> 8);
                    *dst++ = static_cast<uchar>(src[2] >> 8);
                    src += 3;
                }
            }
        }
    }
    raw.dcraw_clear_mem(rgb);
    raw.recycle();
    return out;
}
