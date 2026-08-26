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

    // The size hint and the content rect are two halves of one contract: a control resized to
    // the hint the style produced for a given content must still have room for that content.
    // The content rect always has the padding taken out of it, so the hint has to put it in —
    // deriving the hint from the base style alone reserves only the base style's frame, leaving
    // every form control short by the full padding and clipping its text.
    void test_aSpinBoxSizedToItsHintCanShowItsContent()  // NOLINT
    {
        Gui::FreeCADStyle freecadStyle;
        // The QStyle interface is what the widgets themselves go through.
        QStyle& style = freecadStyle;
        QSpinBox spin;
        style.polish(&spin);

        QStyleOptionSpinBox option;
        option.initFrom(&spin);
        option.frame = true;
        option.subControls = QStyle::SC_SpinBoxFrame | QStyle::SC_SpinBoxEditField
            | QStyle::SC_SpinBoxUp | QStyle::SC_SpinBoxDown;
        option.buttonSymbols = QAbstractSpinBox::UpDownArrows;

        option.rect = QRect(
            QPoint(),
            style.sizeFromContents(QStyle::CT_SpinBox, &option, contentSize(), &spin)
        );
        const QRect editField
            = style.subControlRect(QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxEditField, &spin);

        QVERIFY2(
            editField.width() >= contentWidth,
            qPrintable(QStringLiteral("edit field %1px, content needs %2px")
                           .arg(editField.width())
                           .arg(contentWidth))
        );
    }

    // A spin box with no buttons — the sketcher's on-view parameter editor — sizes itself to
    // exactly its text, so it has no slack to absorb a shortfall.
    void test_aButtonlessSpinBoxSizedToItsHintCanShowItsContent()  // NOLINT
    {
        Gui::FreeCADStyle freecadStyle;
        // The QStyle interface is what the widgets themselves go through.
        QStyle& style = freecadStyle;
        QSpinBox spin;
        spin.setButtonSymbols(QAbstractSpinBox::NoButtons);
        style.polish(&spin);

        QStyleOptionSpinBox option;
        option.initFrom(&spin);
        option.frame = true;
        option.subControls = QStyle::SC_SpinBoxFrame | QStyle::SC_SpinBoxEditField;
        option.buttonSymbols = QAbstractSpinBox::NoButtons;

        option.rect = QRect(
            QPoint(),
            style.sizeFromContents(QStyle::CT_SpinBox, &option, contentSize(), &spin)
        );
        const QRect editField
            = style.subControlRect(QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxEditField, &spin);

        QVERIFY2(
            editField.width() >= contentWidth,
            qPrintable(QStringLiteral("edit field %1px, content needs %2px")
                           .arg(editField.width())
                           .arg(contentWidth))
        );
    }

    // The sketcher's on-view parameter editor asks to be exactly as wide as its own text, so it
    // has no slack anywhere to fall back on, and what matters is the width left *inside* the
    // inner editor: the line edit keeps margins of its own and a column for the cursor, and the
    // text has to fit in what remains.
    void test_anAdjustableQuantitySpinBoxFitsItsUnitSuffix()  // NOLINT
    {
        Gui::FreeCADStyle freecadStyle;
        QStyle& style = freecadStyle;

        Gui::QuantitySpinBox spin;
        spin.setButtonSymbols(QAbstractSpinBox::NoButtons);
        spin.setAutoAdjustWidth(true);
        spin.setMaxExpectedDigits(16);
        spin.setStyle(&freecadStyle);
        style.polish(&spin);
        spin.setValue(Base::Quantity(41.36, Base::Unit::Length));

        spin.resize(spin.sizeHint());
        spin.show();
        QVERIFY(QTest::qWaitForWindowExposed(&spin));

        auto* editor = spin.findChild<QLineEdit*>();
        const QString text = editor->text();
        const QMargins textMargins = editor->textMargins();
        const int cursorWidth = style.pixelMetric(QStyle::PM_TextCursorWidth, nullptr, editor);
        const int usableWidth = editor->width() - textMargins.left() - textMargins.right()
            - cursorWidth;
        const int textWidth = spin.fontMetrics().horizontalAdvance(text);

        QVERIFY2(
            usableWidth >= textWidth,
            qPrintable(QStringLiteral("%1px usable inside the editor, \"%2\" needs %3px")
                           .arg(usableWidth)
                           .arg(text)
                           .arg(textWidth))
        );
    }
};

QTEST_MAIN(TestFormControlSizing)
#include "FormControlSizing.moc"
