/*
 * Copyright (C) 2020 ~ 2020 Deepin Technology Co., Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <QIcon>
#include <QUrlQuery>
#include <QPainter>
#include <DIconTheme>
#include <DDciIcon>
#include <DGuiApplicationHelper>
#include <DPlatformTheme>
#include <QPainterPath>
#include <QtMath>

#include <private/dquickdciiconimage_p.h>

#include "dquickimageprovider_p.h"
#include "dqmlglobalobject_p.h"

#include <cmath>

DQUICK_BEGIN_NAMESPACE
DGUI_USE_NAMESPACE

static inline QImage invalidIcon(QSize *size) {
#ifdef QT_NO_DEBUG
    static QImage invalid(10, 10, QImage::Format_Grayscale8);
    invalid.fill(Qt::black);
    if (size) {
        size->rwidth() = invalid.width();
        size->rheight() = invalid.height();
    }
    return invalid;
#else
    Q_UNUSED(size);
    return QImage();
#endif
}

static QImage requestImageFromQIcon(const QString &id, QSize *size, const QSize &requestedSize) {
    QUrlQuery urlQuery(id);
    QString name = urlQuery.queryItemValue("name");

    if (name.isEmpty())
        return QImage();

    QIcon icon;
    if (auto cached = DIconTheme::cached()) {
        icon = cached->findQIcon(name);
    } else {
        icon = DIconTheme::findQIcon(name);
    }
    if (icon.isNull())
        return invalidIcon(size);

    QIcon::Mode qMode = QIcon::Normal;
    QIcon::State qState = QIcon::Off;

    if (urlQuery.hasQueryItem("mode"))
        qMode = static_cast<QIcon::Mode>(urlQuery.queryItemValue("mode").toInt());

    if (urlQuery.hasQueryItem("state"))
        qState = static_cast<QIcon::State>(urlQuery.queryItemValue("state").toInt());

    // 获取图标的缩放比例
    qreal devicePixelRatio = 1.0;
    if (urlQuery.hasQueryItem("devicePixelRatio")) {
        devicePixelRatio = urlQuery.queryItemValue("devicePixelRatio").toDouble();
        if (qIsNull(devicePixelRatio))
            devicePixelRatio = 1.0;
    }

    QSize icon_size = requestedSize;
    // 初始时可能没有为图标设置期望的大小
    if (icon_size.isEmpty()) {
        icon_size = icon.availableSizes(qMode, qState).first();
    } else {
        // 因为传入的requestedSize是已经乘过缩放的, 因此此处要除以缩放比例获取真实的图标大小
        icon_size /= devicePixelRatio;
    }

    QImage image(icon_size * devicePixelRatio, QImage::Format_ARGB32);
    image.setDevicePixelRatio(devicePixelRatio);
    image.fill(Qt::transparent);
    QPainter painter(&image);

    QColor color(urlQuery.queryItemValue("color"));
    // Fixed filling icon when color is transparent.
    if (color.isValid() && color.alpha() > 0)
        painter.setPen(color);

    // 务必要使用paint的方式获取图片, 有部分可变色的图标类型, 在图标引擎中会使用QPainter::pen的颜色
    // 作为图标的填充颜色
    icon.paint(&painter, QRect(QPoint(0, 0), icon_size), Qt::AlignCenter, qMode, qState);

    if (size)
        *size = icon_size;

    return image;
}

DQuickIconProvider::DQuickIconProvider()
    : QQuickImageProvider(QQuickImageProvider::Image, QQuickImageProvider::ForceAsynchronousImageLoading)
{

}

QImage DQuickIconProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    return requestImageFromQIcon(id, size, requestedSize);
}

DQuickDciIconProvider::DQuickDciIconProvider()
    : QQuickImageProvider(QQuickImageProvider::Image,
                          QQuickImageProvider::ForceAsynchronousImageLoading)
{
}

/*!
    \internal
    \brief A function that handles DCI icon for use in QML.

    This provider is added when the dtk qml plugin construction
    and use to generate the icon resource data needed in the \a id.
    This function adds icon of specified size for the different states,
    topics, types, and other attributes needed in the id.

    \note You should not add a wrong DCI icon here.
 */
QImage DQuickDciIconProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    QUrlQuery urlQuery(id);
    QString name = urlQuery.queryItemValue("name");
    if (name.isEmpty())
        return QImage();

    QString iconPath;
    if (auto cached = DIconTheme::cached()) {
        iconPath = cached->findDciIconFile(name, DGuiApplicationHelper::instance()->applicationTheme()->iconThemeName());
    } else {
        iconPath = DIconTheme::findDciIconFile(name, DGuiApplicationHelper::instance()->applicationTheme()->iconThemeName());
    }

    if (Q_UNLIKELY(iconPath.isEmpty())) {
        // Fallback to normal qicon.
       return requestImageFromQIcon(id, size, requestedSize);
    }
    DDciIcon dciIcon(iconPath);
    if (dciIcon.isNull())
        return requestImageFromQIcon(id, size, requestedSize);

    DDciIcon::Mode mode = DDciIcon::Normal;
    if (urlQuery.hasQueryItem("mode")) {
        int modeValue = urlQuery.queryItemValue("mode").toInt();
        switch (modeValue) {
        case DQMLGlobalObject::NormalState:
            mode = DDciIcon::Normal;
            break;
        case DQMLGlobalObject::DisabledState:
            mode = DDciIcon::Disabled;
            break;
        case DQMLGlobalObject::HoveredState:
            mode = DDciIcon::Hover;
            break;
        case DQMLGlobalObject::PressedState:
            mode = DDciIcon::Pressed;
            break;
        default:
            break;
        }
    }

    int theme = DQuickDciIconImage::UnknowTheme;
    if (urlQuery.hasQueryItem("theme"))
        theme = static_cast<DDciIcon::Theme>(urlQuery.queryItemValue("theme").toInt());

    if (theme == DQuickDciIconImage::UnknowTheme) {
        QColor window = DGuiApplicationHelper::instance()->applicationPalette().window().color();
        theme = DGuiApplicationHelper::toColorType(window);
    }

    // Get the icon pixel ratio
    qreal devicePixelRatio = 1.0;
    if (urlQuery.hasQueryItem("devicePixelRatio")) {
        devicePixelRatio = urlQuery.queryItemValue("devicePixelRatio").toDouble();
        if (qIsNull(devicePixelRatio))
            devicePixelRatio = 1.0;
    }

    DDciIconPalette palette;
    if (urlQuery.hasQueryItem("palette")) {
        palette = DDciIconPalette::convertFromString(urlQuery.queryItemValue("palette"));
    }

    // If the target mode icon didn't found, we should find the normal mode icon
    // and decorate to the target mode.
    // This boundingSize always contains devicePixelRatio.
    int boundingSize = qRound(qMax(requestedSize.width(), requestedSize.height()) / devicePixelRatio);
    QPixmap pixmap = dciIcon.pixmap(devicePixelRatio, boundingSize, DDciIcon::Theme(theme), mode, palette);
    if (pixmap.isNull())
        pixmap = dciIcon.pixmap(devicePixelRatio, boundingSize, DDciIcon::Theme(theme), DDciIcon::Normal, palette);

    if (pixmap.isNull())
        return invalidIcon(size);

    QImage image = pixmap.toImage();
    if (size)
        *size = image.size();
    return image;
}

class ShadowImage : public QSharedData {
public:
    ShadowImage(DQuickShadowProvider *sp, const DQuickShadowProvider::ShadowConfig &c, const QImage &data)
        : sp(sp)
        , config(c)
        , image(data)
    {
        sp->cache.insert(config, this);
    }

    ~ShadowImage() {
        sp->cache.remove(config);
    }

    DQuickShadowProvider *sp;
    DQuickShadowProvider::ShadowConfig config;
    QImage image;
};

DQuickShadowProvider::DQuickShadowProvider()
    : QQuickImageProvider(QQuickImageProvider::Image,
                          QQuickImageProvider::ForceAsynchronousImageLoading)
{

}

DQuickShadowProvider::~DQuickShadowProvider()
{
    qDeleteAll(cache.values());
}

QUrl DQuickShadowProvider::toUrl(qreal boxSize, qreal cornerRadius, qreal shadowBlur, QColor color,
                                 qreal xOffset, qreal yOffset, qreal spread, bool hollow, bool inner)
{
    QUrl url;
    url.setScheme("image");
    url.setHost("dtk.shadow");
    QUrlQuery args;
    args.setQueryItems({
                           {"boxSize", QString::number(boxSize)},
                           {"cornerRadius", QString::number(cornerRadius)},
                           {"shadowBlur", QString::number(shadowBlur)},
                           {"color", color.name(QColor::HexArgb)},
                           {"xOffset", QString::number(xOffset)},
                           {"yOffset", QString::number(yOffset)},
                           {"spread", QString::number(spread)},
                           {"hollow", QString::number(static_cast<uint>(hollow))},
                           {"inner", QString::number(static_cast<uint>(inner))}
                       });
    url.setQuery(args);
    return url;
}

inline static bool getNumberFromString(const QString &string, qreal *number) {
    bool ok = false;
    *number = string.toDouble(&ok);
    return ok;
}

inline static bool getNumberFromString(const QString &string, bool *number) {
    bool ok = false;
    *number = string.toUInt(&ok);
    return ok;
}

static bool fromString(const QString &string, qreal *boxSize, qreal *cornerRadius, qreal *shadowBlur,
                       QColor *color, qreal *xOffset, qreal *yOffset, qreal *spread, bool *hollow, bool *inner)
{
    QUrlQuery args(string);

    if (!getNumberFromString(args.queryItemValue("boxSize"), boxSize))
        return false;
    if (!getNumberFromString(args.queryItemValue("cornerRadius"), cornerRadius))
        return false;
    if (!getNumberFromString(args.queryItemValue("shadowBlur"), shadowBlur))
        return false;
    if (!getNumberFromString(args.queryItemValue("xOffset"), xOffset))
        return false;
    if (!getNumberFromString(args.queryItemValue("yOffset"), yOffset))
        return false;
    if (!getNumberFromString(args.queryItemValue("spread"), spread))
        return false;
    if (!getNumberFromString(args.queryItemValue("hollow"), hollow))
        return false;
    if (!getNumberFromString(args.queryItemValue("inner"), inner))
        return false;

    const QString colorName = args.queryItemValue("color");
    if (colorName.isEmpty())
        return false;

    *color = QColor(colorName);
    if (!color->isValid())
        return false;

    return true;
}

QImage DQuickShadowProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    ShadowConfig config;
    QColor color;
    qreal xOffset;
    qreal yOffset;
    bool hollow;
    bool isInner;

    QImage image;

    do {
        if (!fromString(id, &config.boxSize, &config.cornerRadius, &config.blurRadius,
                        &color, &xOffset, &yOffset, &config.spread, &hollow, &isInner)) {
            break;
        }

        config.type = (requestedSize.width() == requestedSize.height()
                       && requestedSize.width() == qRound(2 * config.cornerRadius)) ? Circular : Rectangle;
        if (isInner)
            config.type |= Inner;

        auto shadow = getRawShadow(config);
        if (!shadow)
            break;

        const qreal shadowSize = config.boxSize + config.blurRadius * (config.isInner() ? 2 : 4)
                + (config.isInner() ? 0 : 2 * config.spread);
        image = QImage(qRound(shadowSize), qRound(shadowSize), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QRectF shadowRect(image.rect());
        shadowRect.moveCenter(QRectF(shadow->image.rect()).center());

        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);

        if (config.cornerRadius > 0) {
            QPainterPath clipPath;
            clipPath.addRoundedRect(image.rect(), config.cornerRadius, config.cornerRadius);
            painter.setClipPath(clipPath);
        }

        QPointF offset(xOffset, yOffset);
        // clear the corners
        if (config.isInner()) {
            painter.fillRect(image.rect(), color);
            shadowRect.translate(-offset);
        }

        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.drawImage(QPointF(0, 0), shadow->image, shadowRect);
        painter.end();

        // draw color
        painter.begin(&image);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(image.rect(), color);
        painter.end();

        if (hollow && !config.isInner()) {
            painter.begin(&image);
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setCompositionMode(QPainter::CompositionMode_Clear);
            painter.setPen(Qt::NoPen);
            painter.setBrush(color);
            QRectF boxRect(0, 0, image.width() - 2 * (config.blurRadius + config.spread),
                           image.height() - 2 * (config.blurRadius + config.spread));
            boxRect.moveCenter(QRectF(image.rect()).center() - offset);
            if (config.cornerRadius > 0) {
                painter.drawRoundedRect(boxRect, config.cornerRadius, config.cornerRadius);
            } else {
                painter.drawRect(boxRect);
            }
            painter.end();
        }
    } while (false);

    if (size)
        *size = image.size();

    return image;
}

/**
 * @brief calculateEffectiveBlurRadius get blur Factor
 * Refer to KDE breeze project and Firefox
 * @param radius shaodw blur Radius
 * @return
 */
static inline int calculateEffectiveBlurRadius(qreal radius)
{
    // https://www.w3.org/TR/css-backgrounds-3/#shadow-blur
    const qreal stdDeviation = radius * 0.5;
    // https://www.w3.org/TR/SVG11/filters.html#feGaussianBlurElement
    const qreal gaussianScaleFactor = (3.0 * M_SQRT2 * qSqrt(M_PI) / 4.0) * 1.5;
    return qMax(2, qFloor(stdDeviation * gaussianScaleFactor + 0.5));
}

typedef QPair<int, int> BoxFilters;

static QVector<BoxFilters> computeBoxFilter(int radius)
{
    int major;
    int minor;
    int final;

    const int averageFactor = 3;
    const int blurRadius = calculateEffectiveBlurRadius(radius);
    const int radiusFactor = blurRadius / averageFactor;

    if (blurRadius % averageFactor < averageFactor) {
        major = radiusFactor + (blurRadius % averageFactor == 0 ? 0 : 1);
        minor = radiusFactor;
        final = radiusFactor + (blurRadius % averageFactor == 2 ? 1 : 0);
    } else {
        Q_UNREACHABLE();
    }

    Q_ASSERT(major + minor + final == blurRadius);

    return {
        {major, minor},
        {minor, major},
        {final, final}
    };
}

static inline void imageConvoluteFilter(const uint8_t *startRow, uint8_t *destination, int rowWidth, int horizontalStride,
                                   int verticalStride, const BoxFilters &boxFilter, bool transposeInput, bool transposeOutput)
{
    const int inputStep = transposeInput ? verticalStride : horizontalStride;
    const int outputStep = transposeOutput ? verticalStride : horizontalStride;
    const int boxSize = boxFilter.first + 1 + boxFilter.second;
    const int reciprocal = (1 << 24) / boxSize;

    uint32_t alphaSum = uint32_t((boxSize + 1) / 2);

    const uint8_t *leftRow = startRow;
    const uint8_t *rightRow = startRow;
    uint8_t *outDestination = destination;

    const uint8_t firstValue = startRow[0];
    const uint8_t lastValue = startRow[(rowWidth - 1) * inputStep];

    alphaSum += uint32_t(firstValue * boxFilter.first);

    const uint8_t *initEnd = startRow + (boxSize - boxFilter.first) * inputStep;
    while (rightRow < initEnd) {
        alphaSum += *rightRow;
        rightRow += inputStep;
    }

    const uint8_t *leftEnd = startRow + boxSize * inputStep;
    while (rightRow < leftEnd) {
        *outDestination = (alphaSum * uint32_t(reciprocal)) >> 24;
        alphaSum += *rightRow - firstValue;
        rightRow += inputStep;
        outDestination += outputStep;
    }

    const uint8_t *centerEnd = startRow + rowWidth * inputStep;
    while (rightRow < centerEnd) {
        *outDestination = (alphaSum * uint32_t(reciprocal)) >> 24;
        alphaSum += *rightRow - *leftRow;
        leftRow += inputStep;
        rightRow += inputStep;
        outDestination += outputStep;
    }

    const uint8_t *rightEnd = destination + rowWidth * outputStep;
    while (outDestination < rightEnd) {
        *outDestination = (alphaSum * uint32_t(reciprocal)) >> 24;
        alphaSum += lastValue - *leftRow;
        leftRow += inputStep;
        outDestination += outputStep;
    }
}

static inline void doBoxShdowBlur(QImage &image, int radius, const QRect &rect = QRect())
{
    if (radius < 2) {
        return;
    }

    const QVector<BoxFilters> filters = computeBoxFilter(radius);
    const QRect blurRect = rect.isNull() ? image.rect() : rect;
    const int alphaOffset = (image.depth() == 8 || QSysInfo::ByteOrder == QSysInfo::BigEndian) ? 0 : 3;
    const int blurWidth = blurRect.width();
    const int blurHeight = blurRect.height();
    const int widthStride = image.bytesPerLine();
    const int pixelBPP = image.depth() >> 3;

    // the amount of memory space occupied by row pixels or column pixels
    const int maxStride = qMax(blurWidth, blurHeight) * pixelBPP;
    QScopedPointer<uint8_t, QScopedPointerArrayDeleter<uint8_t> > twoStride(new uint8_t[2 * maxStride]);
    uint8_t *nextStride = twoStride.data() + maxStride;

    for (int i = 0; i < blurHeight; ++i) {
        uint8_t *row = image.scanLine(blurRect.y() + i) + blurRect.x() * pixelBPP + alphaOffset;
        imageConvoluteFilter(row, twoStride.data(), blurWidth, pixelBPP, widthStride, filters[0], false, false);
        imageConvoluteFilter(twoStride.data(), nextStride, blurWidth, pixelBPP, widthStride, filters[1], false, false);
        imageConvoluteFilter(nextStride, row, blurWidth, pixelBPP, widthStride, filters[2], false, false);
    }

    for (int i = 0; i < blurWidth; ++i) {
        uint8_t *column = image.scanLine(blurRect.y()) + (blurRect.x() + i) * pixelBPP + alphaOffset;
        imageConvoluteFilter(column, twoStride.data(), blurHeight, pixelBPP, widthStride, filters[0], true, false);
        imageConvoluteFilter(twoStride.data(), nextStride, blurHeight, pixelBPP, widthStride, filters[1], false, false);
        imageConvoluteFilter(nextStride, column, blurHeight, pixelBPP, widthStride, filters[2], false, true);
    }
}

static inline void mirrorTopLeftQuadrant(QImage &image, bool horizontal = true, bool vertical = true)
{
    const int width = image.width();
    const int height = image.height();
    const int centerX = qCeil(width * 0.5);
    const int centerY = qCeil(height * 0.5);
    const int alphaOffset = (image.depth() == 8 || QSysInfo::ByteOrder == QSysInfo::BigEndian) ? 0 : 3;
    const int pixelBPP = image.depth() >> 3;

    if (horizontal) {
        for (int y = 0; y < centerY; ++y) {
            uint8_t *sourceAlphaStride = image.scanLine(y) + alphaOffset;
            uint8_t *mirroredPixelPointer = sourceAlphaStride + (width - 1) * pixelBPP;

            for (int x = 0; x < centerX; ++x, sourceAlphaStride += pixelBPP, mirroredPixelPointer -= pixelBPP) {
                *mirroredPixelPointer = *sourceAlphaStride;
            }
        }
    }

    if (!vertical)
        return;

    for (int y = 0; y < centerY; ++y) {
        const uint8_t *sourceAlphaStride = image.scanLine(y) + alphaOffset;
        uint8_t *mirroredPixelPointer = image.scanLine(width - y - 1) + alphaOffset;

        for (int x = 0; x < width; ++x, sourceAlphaStride += pixelBPP, mirroredPixelPointer += pixelBPP) {
            *mirroredPixelPointer = *sourceAlphaStride;
        }
    }
}

static void cleanFunction(void *image) { delete static_cast<QImage*>(image); }

ShadowImage *DQuickShadowProvider::getRawShadow(const ShadowConfig &config)
{
    if (Q_UNLIKELY(qIsNull(config.blurRadius))) {
        return nullptr;
    }

    ShadowImage *image = this->cache.value(config);
    if (Q_LIKELY(image))
        return image;

    const QString path = getShadowFilePath(config);
    if (QFile::exists(path)) {
        QImage *tmp = new QImage(path);
        QImage target(tmp->bits(), tmp->width(), tmp->height(), tmp->bytesPerLine(), QImage::Format_Alpha8, cleanFunction, tmp);
        image = new ShadowImage(this, config, target);
    }

    if (!image) {
        const int effectiveBlurRadius = calculateEffectiveBlurRadius(config.blurRadius);
        const qreal imageSize = (effectiveBlurRadius + config.blurRadius) * 2 + config.boxSize;
        QImage source(qRound(imageSize), qRound(imageSize), QImage::Format_Alpha8);
        source.fill(config.isInner() ? Qt::black : Qt::transparent);

        QRectF boxRect(effectiveBlurRadius, effectiveBlurRadius,
                       imageSize - 2 * effectiveBlurRadius, imageSize - 2 * effectiveBlurRadius);
        QPainter sourcePainter(&source);
        sourcePainter.setRenderHint(QPainter::Antialiasing, true);
        sourcePainter.setPen(Qt::NoPen);
        sourcePainter.setBrush(Qt::black);

        if (config.isInner()) {
            sourcePainter.setCompositionMode(QPainter::CompositionMode_Clear);
            boxRect -= QMarginsF(config.spread, config.spread, config.spread, config.spread);
        } else {
            boxRect += QMarginsF(config.spread, config.spread, config.spread, config.spread);
        }

        if (config.cornerRadius > 0) {
            sourcePainter.drawRoundedRect(boxRect, config.cornerRadius, config.cornerRadius);
        } else {
            sourcePainter.drawRect(boxRect);
        }
        sourcePainter.end();
        const QRect blurRect(0, 0, qCeil(imageSize * 0.5), qCeil(imageSize * 0.5));
        doBoxShdowBlur(source, static_cast<int>(config.blurRadius), blurRect);
        mirrorTopLeftQuadrant(source);

        // you can save the source to the local directory here and add it to the qrc,
        // prevent repeated drawing of shadow pictures.
        // auto alpha = QImage(source.bits(), source.width(), source.height(), source.bytesPerLine(), QImage::Format_Grayscale8);
        // alpha.save("...." + config.toString() + ".png", "PNG");

        image = new ShadowImage(this, config, source);
    }

    return image;
}

DQUICK_END_NAMESPACE