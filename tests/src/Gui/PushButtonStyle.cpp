// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPushButton>
#include <QStyleOptionButton>
#include <QTest>

#include "src/App/InitApplication.h"

#include <Gui/Application.h>
#include <Gui/FreeCADStyle.h>
#include <Gui/StyleParameters/ParameterManager.h>

// The fixture's own numbers, chosen so no two sums collide: a wrong token fails loudly
// instead of coincidentally matching.
constexpr int buttonPaddingH = 12;
constexpr int formControlHeight = 26;
constexpr int contentWidth = 140;
constexpr int contentHeight = 10;

const QColor plainBackground = QColor(QStringLiteral("#101010"));
const QColor primaryBackground = QColor(QStringLiteral("#207020"));
const QColor linkBackground = QColor(QStringLiteral("#3070a0"));
const QColor pressedBackground = QColor(QStringLiteral("#a04040"));

class TestPushButtonStyle: public QObject
{
    Q_OBJECT

public:
    TestPushButtonStyle()
    {
        tests::initApplication();

        if (Gui::Application::Instance == nullptr) {
            new Gui::Application(false);
        }

        Gui::Application::Instance->styleParameterManager()->addSource(
            new Gui::StyleParameters::InMemoryParameterSource(
                {
                    {.name = "FormControlHeight", .value = "26px"},
                    {.name = "ButtonPadding", .value = "padding(horizontal: 12px, vertical: 6px)"},
                    {.name = "ButtonBorderRadius", .value = "0px"},
                    {.name = "ButtonBackground", .value = "#101010"},
                    {.name = "ButtonPrimaryBackground", .value = "#207020"},
                    {.name = "ButtonLinkBackground", .value = "#3070a0"},
                    {.name = "ButtonPressedBackground", .value = "#a04040"},
                },
                {.name = "Push Button Style"}
            )
        );
    }

private Q_SLOTS:

    /// A button asks for its content plus the token padding, raised to the height it
    /// inherits from FormControl.
    void sizeAddsTokenPaddingAndInheritsTheFormControlHeight()
    {
        QPushButton button;
        QStyleOptionButton option;
        option.initFrom(&button);

        const QSize hint = style.sizeFromContents(
            QStyle::CT_PushButton,
            &option,
            QSize(contentWidth, contentHeight),
            &button
        );

        QCOMPARE(hint.width(), contentWidth + (2 * buttonPaddingH));
        QCOMPARE(hint.height(), formControlHeight);
    }

    /// FormControlHeight pins the height rather than raising it, so content taller than
    /// the token does not grow the button. This is what keeps a row of form controls level
    /// whatever each of them holds.
    void theInheritedHeightPinsRatherThanFloors()
    {
        QPushButton button;
        QStyleOptionButton option;
        option.initFrom(&button);

        const QSize hint = style.sizeFromContents(
            QStyle::CT_PushButton,
            &option,
            QSize(contentWidth, formControlHeight * 2),
            &button
        );

        QCOMPARE(hint.height(), formControlHeight);
    }

    /// A default button resolves the Primary variant, so ButtonPrimaryBackground wins.
    void aDefaultButtonPaintsThePrimaryBackground()
    {
        QPushButton button;
        QStyleOptionButton option;
        option.initFrom(&button);
        option.features |= QStyleOptionButton::DefaultButton;

        QCOMPARE(paintedBackground(&button, option), primaryBackground);
    }

    /// A flat button resolves the Link variant.
    void aFlatButtonPaintsTheLinkBackground()
    {
        QPushButton button;
        button.setFlat(true);

        QStyleOptionButton option;
        option.initFrom(&button);

        QCOMPARE(paintedBackground(&button, option), linkBackground);
    }

    /// A plain button resolves neither variant and keeps the base background.
    void aPlainButtonPaintsTheBaseBackground()
    {
        QPushButton button;
        QStyleOptionButton option;
        option.initFrom(&button);

        QCOMPARE(paintedBackground(&button, option), plainBackground);
    }

    /// State_Sunken on a button means pressed. An input widget carries the same flag
    /// permanently, which is why only button-like components map it.
    void aSunkenButtonPaintsThePressedBackground()
    {
        QPushButton button;
        QStyleOptionButton option;
        option.initFrom(&button);
        option.state |= QStyle::State_Sunken;

        QCOMPARE(paintedBackground(&button, option), pressedBackground);
    }

private:
    /// Paints the button panel and reads the colour back off the middle of the box, which is
    /// the same path a real repaint takes.
    QColor paintedBackground(const QWidget* widget, QStyleOptionButton option)
    {
        constexpr int side = 40;

        QImage canvas(side, side, QImage::Format_ARGB32_Premultiplied);
        canvas.fill(Qt::transparent);
        option.rect = QRect(0, 0, side, side);

        QPainter painter(&canvas);
        style.drawPrimitive(QStyle::PE_PanelButtonCommand, &option, &painter, widget);
        painter.end();

        return canvas.pixelColor(side / 2, side / 2);
    }

    Gui::FreeCADStyle style;
};

#include "PushButtonStyle.moc"

QTEST_MAIN(TestPushButtonStyle)
