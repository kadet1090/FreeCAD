// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QStyleOptionToolButton>
#include <QTest>
#include <QToolBar>
#include <QToolButton>

#include "src/App/InitApplication.h"

#include <Gui/Application.h>
#include <Gui/FreeCADStyle.h>
#include <Gui/StyleParameters/ParameterManager.h>

// The fixture's own numbers, chosen so no two sums collide: a wrong token fails loudly
// instead of coincidentally matching.
constexpr int formControlHeight = 26;
constexpr int buttonPadding = 11;
constexpr int toolButtonPadding = 4;
constexpr int contentWidth = 60;
constexpr int contentHeight = 9;

class TestToolButtonStyle: public QObject
{
    Q_OBJECT

public:
    TestToolButtonStyle()
    {
        tests::initApplication();

        if (Gui::Application::Instance == nullptr) {
            new Gui::Application(false);
        }

        Gui::Application::Instance->styleParameterManager()->addSource(
            new Gui::StyleParameters::InMemoryParameterSource(
                {
                    {.name = "FormControlHeight", .value = "26px"},
                    {.name = "ButtonPadding", .value = "padding(11px)"},
                    {.name = "ToolButtonPadding", .value = "padding(4px)"},

                    {.name = "FormControlMinHeight", .value = "@FormControlHeight"},

                    // The toolbar decides its buttons' height, so ToolBarButton stops the
                    // inherited FormControlHeight instead of restating it.
                    {.name = "ToolBarButtonHeight", .value = "reset()"},
                    {.name = "ToolBarButtonMinHeight", .value = "reset()"},
                },
                {.name = "Tool Button Style"}
            )
        );
    }

private Q_SLOTS:

    /// A tool button states its own padding, so it does not take Button's.
    void aToolButtonUsesItsOwnPaddingRatherThanButtons()
    {
        QToolButton button;
        QCOMPARE(hintFor(&button).width(), contentWidth + (2 * toolButtonPadding));
    }

    /// Everything a tool button does not state comes down the chain from Button, which is
    /// what makes the inheritance visible rather than assumed.
    void aToolButtonInheritsTheHeightItDoesNotState()
    {
        QToolButton button;
        QCOMPARE(hintFor(&button).height(), formControlHeight);
    }

    /// A tool button in a toolbar is a different component, and its reset() stops the
    /// inherited height rather than replacing it, so the content decides the height.
    void aToolBarButtonResetsTheInheritedHeight()
    {
        QToolBar toolBar;
        auto* button = new QToolButton(&toolBar);
        toolBar.addWidget(button);

        QCOMPARE(hintFor(button).height(), contentHeight + (2 * toolButtonPadding));
    }

    /// The same widget outside a toolbar still resolves ToolButton, so the branch really
    /// reads the parent rather than something the widget carries.
    void theSameButtonOutsideAToolBarKeepsTheInheritedHeight()
    {
        QToolButton button;
        QToolBar toolBar;

        button.setParent(&toolBar);
        QCOMPARE(hintFor(&button).height(), contentHeight + (2 * toolButtonPadding));

        button.setParent(nullptr);
        QCOMPARE(hintFor(&button).height(), formControlHeight);
    }

    /// The floor the style puts on the widget is reset with the height it comes from: a
    /// toolbar sizes its buttons itself, and a minimum it cannot undo would fight it.
    void aToolBarButtonTakesNoMinimumHeightFloor()
    {
        QToolBar toolBar;
        auto* button = new QToolButton(&toolBar);
        toolBar.addWidget(button);
        QToolButton loose;

        style.polish(button);
        style.polish(&loose);

        QCOMPARE(button->minimumHeight(), 0);
        QCOMPARE(loose.minimumHeight(), formControlHeight);
    }

private:
    QSize hintFor(const QWidget* widget)
    {
        QStyleOptionToolButton option;
        option.initFrom(widget);

        return style.sizeFromContents(
            QStyle::CT_ToolButton,
            &option,
            QSize(contentWidth, contentHeight),
            widget
        );
    }

    Gui::FreeCADStyle style;
};

#include "ToolButtonStyle.moc"

QTEST_MAIN(TestToolButtonStyle)
