// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QCheckBox>
#include <QImage>
#include <QPainter>
#include <QStyleOptionButton>
#include <QTest>
#include <QTreeView>

#include "src/App/InitApplication.h"

#include <Gui/Application.h>
#include <Gui/FreeCADStyle.h>
#include <Gui/StyleParameters/ParameterManager.h>

class TestCheckIndicator: public QObject
{
    Q_OBJECT

public:
    TestCheckIndicator()
    {
        tests::initApplication();

        if (Gui::Application::Instance == nullptr) {
            new Gui::Application(true);
        }

        Gui::Application::Instance->styleParameterManager()->addSource(
            new Gui::StyleParameters::InMemoryParameterSource(
                {
                    {.name = "CheckBoxWidth", .value = "14px"},
                    {.name = "CheckBoxHeight", .value = "14px"},
                    // Wide enough that a tick drawn without it reaches pixels a tick drawn
                    // with it cannot.
                    {.name = "CheckBoxPadding", .value = "padding(4px)"},
                    {.name = "CheckBoxIconColor", .value = "#ff0000"},

                    // A host that names its own component and states the same properties with
                    // different answers. Nothing registers this name, so it resolves the way a
                    // property editor's does: as a prefix in front of the normal chain.
                    {.name = "TestPanelWidth", .value = "40px"},
                    {.name = "TestPanelHeight", .value = "40px"},
                    {.name = "TestPanelPadding", .value = "padding(0px)"},
                },
                {.name = "Check Indicator Fixture"}
            )
        );
    }

private:
    // Paints a checked indicator of the given size on the widget's behalf.
    static QImage paintTick(const Gui::FreeCADStyle& style, const QWidget* widget, int extent)
    {
        QImage image(extent, extent, QImage::Format_ARGB32);
        image.fill(Qt::transparent);

        QStyleOptionButton option;
        option.rect = QRect(0, 0, extent, extent);
        option.state = QStyle::State_Enabled | QStyle::State_On;

        QPainter painter(&image);
        style.drawPrimitive(QStyle::PE_IndicatorCheckBox, &option, &painter, widget);

        return image;
    }

    static bool isTick(QRgb pixel)
    {
        return qAlpha(pixel) > 0 && qRed(pixel) > qGreen(pixel) && qRed(pixel) > qBlue(pixel);
    }

private Q_SLOTS:

    // The indicator inside an item view is a check box, not a smaller copy of the view. A view
    // that names its own component describes itself, and a check indicator the style paints on
    // its behalf must not be measured from that name.
    void test_anIndicatorInsideANamedViewKeepsTheCheckBoxSize()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QTreeView view;
        view.setProperty("component", QStringLiteral("TestPanel"));

        QStyleOptionButton option;

        QCOMPARE(style.pixelMetric(QStyle::PM_IndicatorWidth, &option, &view), 14);
        QCOMPARE(style.pixelMetric(QStyle::PM_IndicatorHeight, &option, &view), 14);
    }

    // Padding is what holds the tick inside its box. Resolved through the host's name it comes
    // back as the host's own, and a tick with no padding fills the box corner to corner.
    void test_anIndicatorInsideANamedViewKeepsTheCheckBoxPadding()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QTreeView view;
        view.setProperty("component", QStringLiteral("TestPanel"));

        const QImage tick = paintTick(style, &view, 14);

        // The upper right corner is where the tick's long arm ends, so it is the first place a
        // tick that lost its padding shows up.
        QVERIFY(!isTick(tick.pixel(12, 1)));
        // The tick is still drawn, just further in.
        QVERIFY(isTick(tick.pixel(9, 5)));
    }

    // The guard is about who the indicator belongs to, not about suppressing the property: a
    // check box carrying the name is the component the name describes.
    void test_aCheckBoxNamingItsOwnComponentStillHonoursIt()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QCheckBox checkBox;
        checkBox.setProperty("component", QStringLiteral("TestPanel"));

        QStyleOptionButton option;

        QCOMPARE(style.pixelMetric(QStyle::PM_IndicatorWidth, &option, &checkBox), 40);
    }
};

QTEST_MAIN(TestCheckIndicator)  // NOLINT
#include "CheckIndicator.moc"
