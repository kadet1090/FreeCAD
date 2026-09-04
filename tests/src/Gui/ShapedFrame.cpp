// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QFrame>
#include <QImage>
#include <QPainter>
#include <QScrollArea>
#include <QStyleOptionFrame>
#include <QTest>

#include "src/App/InitApplication.h"

#include <Gui/Application.h>
#include <Gui/FreeCADStyle.h>
#include <Gui/StyleParameters/ParameterManager.h>

class TestShapedFrame: public QObject
{
    Q_OBJECT

public:
    TestShapedFrame()
    {
        tests::initApplication();

        if (Gui::Application::Instance == nullptr) {
            new Gui::Application(true);
        }

        Gui::Application::Instance->styleParameterManager()->addSource(
            new Gui::StyleParameters::InMemoryParameterSource(
                {
                    // Saturated and opaque, so a painted pixel is unmistakable and a missing
                    // one stays transparent rather than merely off-colour.
                    {.name = "SeparatorColor", .value = "#ff0000"},
                    {.name = "SeparatorThickness", .value = "1px"},
                    {.name = "ListBackground", .value = "#00ff00"},
                    {.name = "ListItemBackground", .value = "#0000ff"},
                },
                {.name = "Shaped Frame Fixture"}
            )
        );
    }

private:
    // Draws the frame the way QFrame::paintEvent does, onto a transparent canvas.
    static QImage paintFrame(const Gui::FreeCADStyle& style, QFrame& frame, QSize size)
    {
        frame.resize(size);

        QImage image(size, QImage::Format_ARGB32);
        image.fill(Qt::transparent);

        QStyleOptionFrame option;
        option.initFrom(&frame);
        option.rect = QRect(QPoint(0, 0), size);
        option.frameShape = frame.frameShape();
        option.lineWidth = frame.lineWidth();

        QPainter painter(&image);
        style.drawControl(QStyle::CE_ShapedFrame, &option, &painter, &frame);

        return image;
    }

private Q_SLOTS:

    // A rule drawn as a frame is the same rule the style draws anywhere else, so it comes from
    // the separator tokens rather than from the palette's sunken frame.
    void test_aHorizontalRuleIsDrawnFromTheSeparatorTokens()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QFrame frame;
        frame.setFrameShape(QFrame::HLine);

        const QImage image = paintFrame(style, frame, {20, 9});

        QCOMPARE(image.pixelColor(10, 4), QColor(Qt::red));
        // One row, not a band: the thickness token is what decides how tall the rule is.
        QCOMPARE(image.pixelColor(10, 0).alpha(), 0);
        QCOMPARE(image.pixelColor(10, 8).alpha(), 0);
    }

    // The same rule turned on its side, so the orientation is read from the shape and not
    // assumed.
    void test_aVerticalRuleIsDrawnFromTheSeparatorTokens()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QFrame frame;
        frame.setFrameShape(QFrame::VLine);

        const QImage image = paintFrame(style, frame, {9, 20});

        QCOMPARE(image.pixelColor(4, 10), QColor(Qt::red));
        QCOMPARE(image.pixelColor(0, 10).alpha(), 0);
        QCOMPARE(image.pixelColor(8, 10).alpha(), 0);
    }

    // A panel is a surface, and a plain frame naming a component is asking for that
    // component's surface rather than for Qt's bevel.
    void test_aPanelIsDrawnAsTheComponentItNames()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QFrame frame;
        frame.setFrameShape(QFrame::StyledPanel);
        frame.setProperty("component", QStringLiteral("List"));

        const QImage image = paintFrame(style, frame, {20, 20});

        QCOMPARE(image.pixelColor(10, 10), QColor(Qt::green));
    }

    // A widget declares what it is, and an element is half of that. Tested on List rather than
    // on Panel so what is under test is the mechanism, not its first caller.
    void test_aFrameIsDrawnAsTheElementItNames()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QFrame frame;
        frame.setFrameShape(QFrame::StyledPanel);
        frame.setProperty("component", QStringLiteral("List"));
        frame.setProperty("element", QStringLiteral("Item"));

        const QImage image = paintFrame(style, frame, {20, 20});

        QCOMPARE(image.pixelColor(10, 10), QColor(Qt::blue));
    }

    // A name no element answers to has to leave the widget at its root element rather than
    // silently selecting some other one.
    void test_aFrameNamingNoKnownElementStaysAtItsRoot()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QFrame frame;
        frame.setFrameShape(QFrame::StyledPanel);
        frame.setProperty("component", QStringLiteral("List"));
        frame.setProperty("element", QStringLiteral("Nonsense"));

        const QImage image = paintFrame(style, frame, {20, 20});

        QCOMPARE(image.pixelColor(10, 10), QColor(Qt::green));
    }

    // The property names what the widget is, not what the style is painting on its behalf. When
    // the style asks this widget for a specific sub-element, that request has to win over the
    // property. (Queried as Shortcut rather than Indicator: contextOf() special-cases
    // element == Indicator for every widget type, ahead of the element property, routing it to
    // CheckBox regardless — a widget-dispatch rule that predates this task and would confound
    // the guard under test here.)
    void test_aDeclaredElementDoesNotAnswerForAnotherOne()  // NOLINT
    {
        QFrame frame;
        frame.setProperty("component", QStringLiteral("List"));
        frame.setProperty("element", QStringLiteral("Item"));

        const auto context = Gui::FreeCADStyle::contextOf(
            &frame,
            nullptr,
            Gui::StyleParameters::StyleComponentElement::Shortcut
        );

        QCOMPARE(context.element, Gui::StyleParameters::StyleComponentElement::Shortcut);
    }

    // A scroll area is a frame too, and one that names a component has to end up with that
    // component's surface behind its content rather than with Qt's palette fill.
    void test_aPlainScrollAreaPaintsItsComponentSurface()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QScrollArea area;
        area.setProperty("component", QStringLiteral("List"));
        area.setStyle(&style);
        area.resize(20, 20);
        style.polish(&area);

        QImage image(area.size(), QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        area.render(&image, QPoint(), QRegion(), QWidget::DrawWindowBackground);

        QCOMPARE(image.pixelColor(2, 2), QColor(Qt::green));
    }

    // A shape the style has nothing to say about has to reach the base style, which is what
    // draws every frame this one does not describe.
    void test_anUnnamedBoxIsLeftToTheBaseStyle()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QFrame frame;
        frame.setFrameShape(QFrame::StyledPanel);

        const QImage image = paintFrame(style, frame, {20, 20});

        QCOMPARE(image.pixelColor(10, 10), QColor(Qt::transparent));
    }
};

QTEST_MAIN(TestShapedFrame)  // NOLINT
#include "ShapedFrame.moc"
