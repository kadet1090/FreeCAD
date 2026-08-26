// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QStyleOptionComboBox>
#include <QStyleOptionFrame>
#include <QStyleOptionSpinBox>
#include <QStyle>
#include <QTest>

#include "src/App/InitApplication.h"

#include <Gui/Application.h>
#include <Gui/FreeCADStyle.h>
#include <Gui/QuantitySpinBox.h>
#include <Gui/StyleParameters/ParameterManager.h>

// The horizontal padding the fixture gives every form control, per side.
constexpr int paddingPerSide = 8;
// A content width wide enough that the graceful-degradation path in contentRect() — which
// shrinks the padding on controls too small to hold it — never engages.
constexpr int contentWidth = 100;
constexpr int contentHeight = 18;

class TestFormControlSizing: public QObject
{
    Q_OBJECT

public:
    TestFormControlSizing()
    {
        tests::initApplication();

        if (Gui::Application::Instance == nullptr) {
            new Gui::Application(true);
        }

        // The size-relevant slice of "FreeCAD Base.yaml".
        Gui::Application::Instance->styleParameterManager()->addSource(
            new Gui::StyleParameters::InMemoryParameterSource(
                {
                    {.name = "FormControlHeight", .value = "26px"},
                    {.name = "FormControlPadding", .value = "padding(horizontal: 8px, vertical: 6px)"},
                    {.name = "LineEditHeight", .value = "@FormControlHeight"},
                    {.name = "LineEditPadding", .value = "@FormControlPadding"},
                },
                {.name = "Form Control Sizing"}
            )
        );

        // Registered last so it outranks the fixture above, and left empty so it costs nothing
        // until a test asks for a different value.
        overrides = new Gui::StyleParameters::InMemoryParameterSource(
            {},
            {.name = "Form Control Overrides"}
        );
        Gui::Application::Instance->styleParameterManager()->addSource(overrides);
    }

private:
    Gui::StyleParameters::InMemoryParameterSource* overrides = nullptr;

    // Swaps one token in for the body of a test and puts the fixture's value back on the way
    // out, so an assertion that returns early cannot leak it into the next test.
    [[nodiscard]] auto overrideToken(const std::string& name, const std::string& value) const
    {
        auto* manager = Gui::Application::Instance->styleParameterManager();

        overrides->define({.name = name, .value = value});
        manager->reload();

        return qScopeGuard([this, manager, name] {
            overrides->remove(name);
            manager->reload();
        });
    }

    static QSize contentSize()
    {
        return {contentWidth, contentHeight};
    }

private Q_SLOTS:

    void test_aLineEditSizedToItsHintCanShowItsContent()  // NOLINT
    {
        Gui::FreeCADStyle freecadStyle;
        // The QStyle interface is what the widgets themselves go through.
        QStyle& style = freecadStyle;
        QLineEdit edit;
        style.polish(&edit);

        QStyleOptionFrame option;
        option.initFrom(&edit);
        // A framed line edit; the spin box's inner editor reports lineWidth 0 and is laid out
        // by the spin box instead.
        option.lineWidth = 1;

        option.rect = QRect(
            QPoint(),
            style.sizeFromContents(QStyle::CT_LineEdit, &option, contentSize(), &edit)
        );
        const QRect contents = style.subElementRect(QStyle::SE_LineEditContents, &option, &edit);

        QVERIFY2(
            contents.width() >= contentWidth,
            qPrintable(
                QStringLiteral("contents %1px, content needs %2px").arg(contents.width()).arg(contentWidth)
            )
        );
    }

    // The padding is added before the clamps, never instead of them: a control with a maximum
    // width still honours it, so a hint can never grow past what the theme allows.
    void test_theMaximumWidthTokenStillClampsThePaddedHint()  // NOLINT
    {
        const auto restore = overrideToken("LineEditMaxWidth", "40px");

        Gui::FreeCADStyle freecadStyle;
        QStyle& style = freecadStyle;
        QLineEdit edit;
        style.polish(&edit);

        QStyleOptionFrame option;
        option.initFrom(&edit);
        option.lineWidth = 1;

        QCOMPARE(style.sizeFromContents(QStyle::CT_LineEdit, &option, contentSize(), &edit).width(), 40);
    }

    // The height token is a fixed size rather than a floor, and stays one: the vertical padding
    // is added first but the clamp still decides the final height.
    void test_theHeightTokenStillPinsTheHint()  // NOLINT
    {
        Gui::FreeCADStyle freecadStyle;
        QStyle& style = freecadStyle;
        QLineEdit edit;
        style.polish(&edit);

        QStyleOptionFrame option;
        option.initFrom(&edit);
        option.lineWidth = 1;

        QCOMPARE(
            style.sizeFromContents(QStyle::CT_LineEdit, &option, contentSize(), &edit).height(),
            26
        );
    }

    // Pins the mechanism rather than only the symptom: the padding token is what the hint has
    // to account for, so dropping it must shrink the hint by exactly that much.
    void test_theHintTracksTheHorizontalPaddingToken()  // NOLINT
    {
        Gui::FreeCADStyle freecadStyle;
        // The QStyle interface is what the widgets themselves go through.
        QStyle& style = freecadStyle;
        QLineEdit edit;
        style.polish(&edit);

        QStyleOptionFrame option;
        option.initFrom(&edit);
        option.lineWidth = 1;

        const int paddedWidth
            = style.sizeFromContents(QStyle::CT_LineEdit, &option, contentSize(), &edit).width();

        const auto restore
            = overrideToken("FormControlPadding", "padding(horizontal: 0px, vertical: 6px)");
        // A second style rather than the first one again: the token cache is per style object,
        // and only a theme reload clears it in production.
        Gui::FreeCADStyle unpaddedFreecadStyle;
        QStyle& unpaddedStyle = unpaddedFreecadStyle;
        const int unpaddedWidth
            = unpaddedStyle.sizeFromContents(QStyle::CT_LineEdit, &option, contentSize(), &edit).width();

        QCOMPARE(paddedWidth - unpaddedWidth, 2 * paddingPerSide);
    }
};

QTEST_MAIN(TestFormControlSizing)
#include "FormControlSizing.moc"
