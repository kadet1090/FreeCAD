// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QTest>

#include <Gui/ElideLabel.h>

class TestElideLabel: public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // The label paints its own text instead of letting QLabel do it, so the rect it hands the
    // painter is the only thing that can move the glyphs. That rect is invisible to every
    // widget-geometry assertion — layout margins, spacing, geometry() — because the label's own
    // rect is positioned correctly either way; only what it paints inside it can be off. So this
    // renders the label and finds the column the first glyph pixel actually lands on, and
    // compares it against the same string drawn by the same painter call at column zero, rather
    // than a hardcoded number that would be fragile against hinting and side bearings.
    void test_paintsTextFlushWithItsLeftEdge()  // NOLINT
    {
        Gui::ElideLabel label;
        label.setText(sampleText());
        label.resize(300, 20);

        const QColor textColor = label.palette().color(QPalette::WindowText);
        const int paintedColumn = firstGlyphColumn(render(label), textColor);
        const int controlColumn = firstGlyphColumn(control(label), textColor);

        QVERIFY(controlColumn >= 0);
        QCOMPARE(paintedColumn, controlColumn);
    }

    // The width the text is elided against is the same one it is drawn into. A label holding part
    // of that width back for an inset it no longer paints would drop characters it has room for.
    void test_elidesAgainstItsFullWidth()  // NOLINT
    {
        Gui::ElideLabel label;
        label.setText(sampleText());

        // The narrowest width QFontMetrics::elidedText leaves the string whole at — one pixel
        // over its advance. Nothing to elide here, and no room to spare either: an inset the
        // label held back would cut characters it has the width for.
        label.resize(QFontMetrics(label.font()).horizontalAdvance(sampleText()) + 1, 20);

        QCOMPARE(render(label), control(label));
    }

private:
    // Long enough that an ellipsis is unmistakable.
    static QString sampleText()
    {
        return QStringLiteral("Base profile types");
    }

    static QImage render(Gui::ElideLabel& label)
    {
        QImage canvas(label.size(), QImage::Format_ARGB32);
        canvas.fill(ground(label));
        label.render(&canvas);

        return canvas;
    }

    // The same string, drawn by the same painter call the label makes, over the label's whole
    // rect — what the label paints if it adds nothing of its own.
    static QImage control(const Gui::ElideLabel& label)
    {
        QImage canvas(label.size(), QImage::Format_ARGB32);
        canvas.fill(ground(label));

        {
            QPainter painter(&canvas);
            painter.setPen(label.palette().color(QPalette::WindowText));
            painter.setFont(label.font());
            painter.drawText(canvas.rect(), Qt::AlignLeft | Qt::AlignVCenter, label.text());
        }

        return canvas;
    }

    // QWidget::render() lays the palette's window brush down before the label paints, so the
    // control has to start from the same ground for the two to be comparable pixel for pixel.
    static QColor ground(const Gui::ElideLabel& label)
    {
        return label.palette().color(QPalette::Window);
    }

    // Scans left to right for the first column holding an exact @p textColor pixel — the leftmost
    // point a glyph is actually painted, as opposed to any rect a layout merely positioned.
    static int firstGlyphColumn(const QImage& canvas, const QColor& textColor)
    {
        for (int x = 0; x < canvas.width(); ++x) {
            for (int y = 0; y < canvas.height(); ++y) {
                if (canvas.pixelColor(x, y) == textColor) {
                    return x;
                }
            }
        }

        return -1;
    }
};

QTEST_MAIN(TestElideLabel)

#include "ElideLabel.moc"
