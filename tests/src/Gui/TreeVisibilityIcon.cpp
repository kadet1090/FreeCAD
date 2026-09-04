// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QIcon>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QTest>

#include <Gui/Tree.h>

namespace
{

/// Logical side of the object icon the tree rasterises. Whole device pixels at every ratio here.
constexpr int IconExtent = 16;

/// A second, deliberately different extent. The tree's stated icon height and the size its icons
/// are actually built at do not have to agree, so nothing may size the composite from a constant
/// of its own.
constexpr int SmallerExtent = 12;

/// Gap between the two cells. Also chosen to land the object cell on a whole device pixel.
constexpr int CellSpacing = 4;

constexpr QRgb OddColumn = qRgb(0, 0, 255);
constexpr QRgb EvenColumn = qRgb(255, 0, 0);

/// An object icon as the tree builds it: @p extent logical pixels, rasterised for @p pixelRatio
/// and tagged with that ratio.
///
/// Its columns alternate colour every single device pixel, so any resampling of the composite
/// shows up as a pixel that no longer matches the source.
QIcon stripedIcon(int extent, qreal pixelRatio)
{
    QImage image(QSize(extent, extent) * pixelRatio, QImage::Format_ARGB32_Premultiplied);
    for (int row = 0; row < image.height(); ++row) {
        for (int column = 0; column < image.width(); ++column) {
            image.setPixel(column, row, (column % 2) != 0 ? OddColumn : EvenColumn);
        }
    }

    QPixmap pixmap = QPixmap::fromImage(image);
    pixmap.setDevicePixelRatio(pixelRatio);

    QIcon icon;
    icon.addPixmap(pixmap, QIcon::Normal, QIcon::Off);
    return icon;
}

/// The device pixels of @p icon at @p pixelRatio, which is what the composite has to reproduce.
QImage sourcePixels(const QIcon& icon, int extent, qreal pixelRatio)
{
    return icon.pixmap(QSize(extent, extent), pixelRatio, QIcon::Normal, QIcon::Off).toImage();
}

Gui::VisibilityIconLayout layoutAt(qreal pixelRatio)
{
    return {.spacing = CellSpacing, .pixelRatio = pixelRatio};
}

/// Where the object cell starts in the composite's device pixels.
int cellOffsetAt(int extent, qreal pixelRatio)
{
    return qRound((extent + CellSpacing) * pixelRatio);
}

/// How many device pixels of the composite's object cell differ from the source they came from.
int resampledPixels(const QImage& composite, const QImage& source, int cellOffset)
{
    int differing = 0;
    for (int row = 0; row < source.height(); ++row) {
        for (int column = 0; column < source.width(); ++column) {
            if (composite.pixel(column + cellOffset, row) != source.pixel(column, row)) {
                ++differing;
            }
        }
    }
    return differing;
}

}  // namespace

class TestTreeVisibilityIcon: public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // QIcon reports a pixmap-backed icon in device pixels. Every layout below is in logical ones,
    // so the conversion is the whole of what keeps the two apart.
    void test_reportsAnIconsSizeInLogicalPixels()  // NOLINT
    {
        QFETCH(qreal, pixelRatio);

        const QIcon icon = stripedIcon(IconExtent, pixelRatio);

        QCOMPARE(Gui::logicalIconSize(icon, pixelRatio, QIcon::Off), QSize(IconExtent, IconExtent));
    }

    void test_reportsAnIconsSizeInLogicalPixels_data()  // NOLINT
    {
        ratios();
    }

    // The composite is laid out in logical pixels, so it reserves one cell per icon whatever the
    // display ratio. Sized from the source's device pixels instead it grows with the ratio, and
    // the tree then scales the whole thing back down to the row's icon height.
    void test_reservesOneLogicalCellPerIcon()  // NOLINT
    {
        QFETCH(qreal, pixelRatio);

        const QPixmap composite = Gui::composeVisibilityIcon(
            QIcon(),
            stripedIcon(IconExtent, pixelRatio),
            QIcon::Off,
            layoutAt(pixelRatio)
        );

        QCOMPARE(composite.devicePixelRatio(), pixelRatio);
        QCOMPARE(
            composite.deviceIndependentSize().toSize(),
            QSize((2 * IconExtent) + CellSpacing, IconExtent)
        );
    }

    void test_reservesOneLogicalCellPerIcon_data()  // NOLINT
    {
        ratios();
    }

    // The cell comes from the icon and from nothing else. Sized from the tree's stated icon height
    // instead, an icon built smaller than that is asked for pixels it does not have — QIcon then
    // answers with a ratio-1 pixmap, and the composite stretches it by the display ratio.
    void test_sizesTheCellFromTheIconNotFromAConstant()  // NOLINT
    {
        const qreal pixelRatio = 1.25;

        for (int extent : {IconExtent, SmallerExtent}) {
            const QIcon icon = stripedIcon(extent, pixelRatio);
            const QPixmap composite
                = Gui::composeVisibilityIcon(QIcon(), icon, QIcon::Off, layoutAt(pixelRatio));

            QCOMPARE(
                composite.deviceIndependentSize().toSize(),
                QSize((2 * extent) + CellSpacing, extent)
            );
            QCOMPARE(
                resampledPixels(
                    composite.toImage(),
                    sourcePixels(icon, extent, pixelRatio),
                    cellOffsetAt(extent, pixelRatio)
                ),
                0
            );
        }
    }

    // The object icon arrives already rasterised for the display. Drawing it into a cell measured
    // off its own device pixels stretches it by the display ratio — the icon carries no more
    // detail than it did, so the extra pixels are interpolated and the glyph goes soft. At a
    // fractional ratio nothing downstream undoes that stretch, which is why 125% shows it worst.
    void test_drawsTheObjectIconWithoutResamplingIt()  // NOLINT
    {
        QFETCH(qreal, pixelRatio);

        const QIcon icon = stripedIcon(IconExtent, pixelRatio);
        const QPixmap composite
            = Gui::composeVisibilityIcon(QIcon(), icon, QIcon::Off, layoutAt(pixelRatio));

        const int differing = resampledPixels(
            composite.toImage(),
            sourcePixels(icon, IconExtent, pixelRatio),
            cellOffsetAt(IconExtent, pixelRatio)
        );

        QCOMPARE(differing, 0);
    }

    void test_drawsTheObjectIconWithoutResamplingIt_data()  // NOLINT
    {
        ratios();
    }

    // An object whose visibility cannot be toggled still reserves the marker's cell, so its icon
    // stays in the same column as every row that does show one.
    void test_keepsTheObjectColumnWithoutAMarker()  // NOLINT
    {
        const qreal pixelRatio = 1.25;
        const QIcon icon = stripedIcon(IconExtent, pixelRatio);

        const QPixmap withoutMarker
            = Gui::composeVisibilityIcon(QIcon(), icon, QIcon::Off, layoutAt(pixelRatio));
        const QPixmap withMarker
            = Gui::composeVisibilityIcon(icon, icon, QIcon::Off, layoutAt(pixelRatio));

        QCOMPARE(withoutMarker.size(), withMarker.size());

        const QImage source = sourcePixels(icon, IconExtent, pixelRatio);
        const int cellOffset = cellOffsetAt(IconExtent, pixelRatio);
        QCOMPARE(resampledPixels(withoutMarker.toImage(), source, cellOffset), 0);
        QCOMPARE(resampledPixels(withMarker.toImage(), source, cellOffset), 0);
    }

private:
    // The ratios desktops actually offer, at which a 16px icon still lands on whole device pixels.
    static void ratios()
    {
        QTest::addColumn<qreal>("pixelRatio");
        QTest::newRow("100%") << 1.0;
        QTest::newRow("125%") << 1.25;
        QTest::newRow("150%") << 1.5;
        QTest::newRow("200%") << 2.0;
    }
};

QTEST_MAIN(TestTreeVisibilityIcon)

#include "TreeVisibilityIcon.moc"
