// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QStyleOptionComboBox>
#include <QStyleOptionFrame>
#include <QStyleOptionSpinBox>
#include <QStyle>
#include <QStyleFactory>
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
                    {.name = "FormControlMinHeight", .value = "@FormControlHeight"},
                    {.name = "FormControlPadding", .value = "padding(horizontal: 8px, vertical: 6px)"},
                    {.name = "LineEditHeight", .value = "@FormControlHeight"},
                    {.name = "LineEditPadding", .value = "@FormControlPadding"},
                    {.name = "FormControlArrowWidth", .value = "14px"},
                    {.name = "FormControlArrowHeight", .value = "12px"},
                    {.name = "SelectPadding", .value = "@FormControlPadding"},
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

    // FreeCAD sets a style sheet at startup, so QStyleSheetStyle wraps the application style for
    // the whole running application, and its CT_SpinBox reserves a step-button column of its own
    // on top of the one this style already charged for. The surplus lands in the editor, where it
    // reads as a field holding several characters more than any token asked for.
    void test_aStyleSheetDoesNotWidenASpinBox()  // NOLINT
    {
        // The application style, not a per-widget one: a style set on a single widget is never
        // wrapped, so setStyle() here would measure the same style twice and prove nothing. And
        // the application's own FreeCADStyle instance, not a standalone one: isWrappedFor() only
        // ever finds a wrapped style by asking Application::Instance for it, so installing any
        // other instance would leave the base style it detects behind the sheet unrecognised.
        const QString previous = QApplication::style()->name();
        const auto restore = qScopeGuard([&previous] {
            qApp->setStyleSheet(QString());
            QApplication::setStyle(QStyleFactory::create(previous));
        });
        Gui::Application::Instance->setStyle(QStringLiteral("FreeCAD"));

        QSpinBox spin;
        spin.ensurePolished();

        // Before the sheet goes on: proves the application style really is FreeCADStyle, not
        // merely something other than FreeCADStyle. Without this, a setStyle() that silently
        // no-ops would leave both measurements on Fusion, equal, and the test green while
        // proving nothing.
        QVERIFY2(
            qobject_cast<Gui::FreeCADStyle*>(spin.style()) != nullptr,
            "the application style is not FreeCADStyle, so the test proves nothing"
        );

        const int bare = spin.sizeHint().width();

        QStyleOptionSpinBox option;
        option.initFrom(&spin);
        option.frame = true;
        option.subControls = QStyle::SC_SpinBoxFrame | QStyle::SC_SpinBoxEditField
            | QStyle::SC_SpinBoxUp | QStyle::SC_SpinBoxDown;
        option.buttonSymbols = QAbstractSpinBox::UpDownArrows;
        option.rect = QRect(QPoint(), QSize(bare, contentHeight + 2 * paddingPerSide));
        const QRect bareArrow
            = spin.style()->subControlRect(QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxUp, &spin);

        qApp->setStyleSheet(QStringLiteral("/* */"));
        spin.ensurePolished();

        QVERIFY2(
            qobject_cast<Gui::FreeCADStyle*>(spin.style()) == nullptr,
            "the sheet did not wrap the style, so the test proves nothing"
        );
        QCOMPARE(spin.sizeHint().width(), bare);

        // And the width has to come back without costing the buttons the rect Qt hit-tests step
        // clicks against. Cancelling the column from the sheet itself does exactly that, which is
        // why the style subtracts it rather than asking the sheet not to add it.
        option.rect = QRect(QPoint(), QSize(spin.sizeHint().width(), option.rect.height()));
        const QRect sheetedArrow
            = spin.style()->subControlRect(QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxUp, &spin);

        QVERIFY(sheetedArrow.width() > 0);
        QCOMPARE(sheetedArrow.width(), bareArrow.width());
    }

    // How much the sheet reserves is not a constant to be known in advance: across 6.8 to 6.11
    // Qt has made it an unconditional 16px, then nothing at all, then 16px again, and a sheet
    // that sizes the buttons itself makes it whatever that rule asks for. Only giving back what
    // the sheet was seen to add survives all four, so a sheet naming a width the style cannot
    // have guessed is what tells a measured compensation apart from a hardcoded one.
    void test_aStyleSheetSizingTheStepButtonsDoesNotResizeASpinBox()  // NOLINT
    {
        const QString previous = QApplication::style()->name();
        const auto restore = qScopeGuard([&previous] {
            qApp->setStyleSheet(QString());
            QApplication::setStyle(QStyleFactory::create(previous));
        });
        Gui::Application::Instance->setStyle(QStringLiteral("FreeCAD"));

        QSpinBox spin;
        spin.ensurePolished();

        QVERIFY2(
            qobject_cast<Gui::FreeCADStyle*>(spin.style()) != nullptr,
            "the application style is not FreeCADStyle, so the test proves nothing"
        );

        const int bare = spin.sizeHint().width();

        // The border is what makes the rule one Qt would paint, and only a rule Qt would paint
        // gets its width honoured; the width itself is simply not the 16px Qt falls back to.
        qApp->setStyleSheet(
            QStringLiteral("QAbstractSpinBox::up-button { width: 4px; border: none; }")
        );
        spin.ensurePolished();

        QVERIFY2(
            qobject_cast<Gui::FreeCADStyle*>(spin.style()) == nullptr,
            "the sheet did not wrap the style, so the test proves nothing"
        );
        QCOMPARE(spin.sizeHint().width(), bare);
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

    // The column the step arrows sit in is the style's to size, not the base style's: Fusion
    // reserves a wider one than the arrows FreeCADStyle actually draws, and a QStyleSheetStyle
    // over the top reserves a third. Charging it to a token is what makes the reservation and
    // the arrow rect the same number.
    void test_theArrowColumnIsWorthExactlyItsToken()  // NOLINT
    {
        // A style per measurement: it caches every token it resolves, so one built before an
        // override would answer from the value the override replaced.
        const auto arrowColumn = [] {
            Gui::FreeCADStyle freecadStyle;
            QStyle& style = freecadStyle;
            QSpinBox spin;
            style.polish(&spin);

            const auto width = [&](QAbstractSpinBox::ButtonSymbols symbols) {
                QStyleOptionSpinBox option;
                option.initFrom(&spin);
                option.frame = true;
                option.subControls = QStyle::SC_SpinBoxFrame | QStyle::SC_SpinBoxEditField
                    | QStyle::SC_SpinBoxUp | QStyle::SC_SpinBoxDown;
                option.buttonSymbols = symbols;

                return style.sizeFromContents(QStyle::CT_SpinBox, &option, contentSize(), &spin).width();
            };

            return width(QAbstractSpinBox::UpDownArrows) - width(QAbstractSpinBox::NoButtons);
        };

        QCOMPARE(arrowColumn(), 14);

        const auto restore = overrideToken("LineEditArrowWidth", "30px");
        QCOMPARE(arrowColumn(), 30);
    }

    // One number moves every control's arrow column. A spin box and a combo box that disagree
    // about it read as two different controls sitting in the same panel.
    void test_theArrowColumnComesFromTheSharedToken()  // NOLINT
    {
        // A style per measurement: it caches every token it resolves, so one built before an
        // override would answer from the value the override replaced.
        const auto columnWidth = [] {
            Gui::FreeCADStyle freecadStyle;
            QStyle& style = freecadStyle;
            QSpinBox spin;
            style.polish(&spin);

            const auto width = [&](QAbstractSpinBox::ButtonSymbols symbols) {
                QStyleOptionSpinBox option;
                option.initFrom(&spin);
                option.frame = true;
                option.subControls = QStyle::SC_SpinBoxFrame | QStyle::SC_SpinBoxEditField
                    | QStyle::SC_SpinBoxUp | QStyle::SC_SpinBoxDown;
                option.buttonSymbols = symbols;

                return style.sizeFromContents(QStyle::CT_SpinBox, &option, contentSize(), &spin).width();
            };

            return width(QAbstractSpinBox::UpDownArrows) - width(QAbstractSpinBox::NoButtons);
        };

        QCOMPARE(columnWidth(), 14);

        const auto restore = overrideToken("FormControlArrowWidth", "30px");
        QCOMPARE(columnWidth(), 30);
    }

    // The whole of the box has an owner: padding, the arrow column, and the editor between
    // them. Any width nobody accounted for lands in the editor, where it reads as a field that
    // will not shrink and holds several digits more than the theme ever budgeted for.
    void test_theEditFieldKeepsEverythingButThePaddingAndTheArrowColumn()  // NOLINT
    {
        Gui::FreeCADStyle freecadStyle;
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
        const QRect arrow
            = style.subControlRect(QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxUp, &spin);

        QCOMPARE(editField.left(), paddingPerSide);
        QCOMPARE(arrow.width(), 14);
        // The fixture's FormControlArrowHeight, not the edit field's content height: the two
        // happen to differ (12px vs. 18px) precisely so a rect that fell back to the content
        // height instead of the token would be caught here.
        QCOMPARE(arrow.height(), 12);
        QCOMPARE(arrow.right(), option.rect.right() - paddingPerSide);
        QCOMPARE(editField.right(), arrow.left() - 1);

        // Exactly the content it was sized for, not a pixel more: any width reserved by a term
        // nothing lays out in reaches the editor as invisible slack.
        QCOMPARE(editField.width(), contentWidth);

        // A fixed 12 alone would also match a hardcoded fallback that happened to equal the
        // fixture's number, so suppress the token itself with reset() — the sanctioned way to
        // make a token resolve to nothing, in place of hand-editing the fixture — and check the
        // rect falls back to arrowColumnSize()'s documented (0, 0), not to some other literal
        // that would silently take its place. A style per measurement, same as the column-width
        // tests above: FreeCADStyle caches every token it resolves, so reusing `style` here
        // would still answer 12.
        const auto restoreHeight = overrideToken("FormControlArrowHeight", "reset()");

        Gui::FreeCADStyle unsetStyle;
        QStyle& unsetQStyle = unsetStyle;
        QSpinBox unsetSpin;
        unsetQStyle.polish(&unsetSpin);

        QStyleOptionSpinBox unsetOption;
        unsetOption.initFrom(&unsetSpin);
        unsetOption.frame = true;
        unsetOption.subControls = QStyle::SC_SpinBoxFrame | QStyle::SC_SpinBoxEditField
            | QStyle::SC_SpinBoxUp | QStyle::SC_SpinBoxDown;
        unsetOption.buttonSymbols = QAbstractSpinBox::UpDownArrows;
        unsetOption.rect = QRect(
            QPoint(),
            unsetQStyle.sizeFromContents(QStyle::CT_SpinBox, &unsetOption, contentSize(), &unsetSpin)
        );

        const QRect unsetArrow = unsetQStyle.subControlRect(
            QStyle::CC_SpinBox,
            &unsetOption,
            QStyle::SC_SpinBoxUp,
            &unsetSpin
        );
        QCOMPARE(unsetArrow.height(), 0);
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

    // FreeCAD sets a style sheet at startup, so QStyleSheetStyle wraps the application style for
    // the whole running application — and its CT_SpinBox reserves a step-arrow column of its own
    // on top of the one this style already charged for. A box that asked the wrapper would be
    // that much wider than the theme budgeted, and the surplus lands in the editor, where it
    // reads as a field holding several digits more than any token asked for.
    void test_aStyleSheetDoesNotWidenAQuantitySpinBox()  // NOLINT
    {
        // The application style, not a per-widget one: a style set on a single widget is never
        // wrapped, so setStyle() here would measure the same style twice and prove nothing. And
        // the application's own FreeCADStyle instance, not a standalone one: isWrappedFor() only
        // ever finds a wrapped style by asking Application::Instance for it, so installing any
        // other instance would leave the base style it detects behind the sheet unrecognised.
        const QString previous = QApplication::style()->name();
        const auto restore = qScopeGuard([&previous] {
            qApp->setStyleSheet(QString());
            QApplication::setStyle(QStyleFactory::create(previous));
        });
        Gui::Application::Instance->setStyle(QStringLiteral("FreeCAD"));

        Gui::QuantitySpinBox spin;
        spin.setUnit(Base::Unit::Length);
        spin.ensurePolished();

        // Before the sheet goes on: proves the application style really is FreeCADStyle, not
        // merely something other than FreeCADStyle. Without this, a setStyle() that silently
        // no-ops would leave both measurements on Fusion, equal, and the test green while
        // proving nothing.
        QVERIFY2(
            qobject_cast<Gui::FreeCADStyle*>(spin.style()) != nullptr,
            "the application style is not FreeCADStyle, so the test proves nothing"
        );

        const int bare = spin.sizeHint().width();

        qApp->setStyleSheet(QStringLiteral("/* */"));
        spin.ensurePolished();

        QVERIFY2(
            qobject_cast<Gui::FreeCADStyle*>(spin.style()) == nullptr,
            "the sheet did not wrap the style, so the test proves nothing"
        );
        QCOMPARE(spin.sizeHint().width(), bare);
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

    // Padding wider than the fixture's, so a padding term dropped from the width calculation
    // would show up as a shortfall rather than being absorbed by the fixture's own margin.
    void test_aComboBoxSizedToItsHintCanShowItsContent()  // NOLINT
    {
        const auto restore
            = overrideToken("FormControlPadding", "padding(horizontal: 24px, vertical: 6px)");

        Gui::FreeCADStyle freecadStyle;
        // The QStyle interface is what the widgets themselves go through.
        QStyle& style = freecadStyle;
        QComboBox combo;
        style.polish(&combo);

        QStyleOptionComboBox option;
        option.initFrom(&combo);
        option.frame = true;
        option.subControls = QStyle::SC_ComboBoxFrame | QStyle::SC_ComboBoxEditField
            | QStyle::SC_ComboBoxArrow;

        option.rect = QRect(
            QPoint(),
            style.sizeFromContents(QStyle::CT_ComboBox, &option, contentSize(), &combo)
        );
        const QRect editField
            = style.subControlRect(QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxEditField, &combo);

        QVERIFY2(
            editField.width() >= contentWidth,
            qPrintable(QStringLiteral("edit field %1px, content needs %2px")
                           .arg(editField.width())
                           .arg(contentWidth))
        );
    }

    // The two controls sit side by side in every task panel, so their arrows have to occupy the
    // same column and leave their editors ending at the same offset. Before this, a combo box's
    // arrow came from the base style and measured 12x14 against a spin box's 14x12.
    void test_aComboBoxAndASpinBoxShareTheirArrowColumn()  // NOLINT
    {
        Gui::FreeCADStyle freecadStyle;
        QStyle& style = freecadStyle;

        QComboBox combo;
        combo.addItem(QStringLiteral("An item"));
        style.polish(&combo);
        QStyleOptionComboBox comboOption;
        comboOption.initFrom(&combo);
        comboOption.frame = true;
        comboOption.subControls = QStyle::SC_ComboBoxFrame | QStyle::SC_ComboBoxEditField
            | QStyle::SC_ComboBoxArrow;
        comboOption.rect = QRect(
            QPoint(),
            style.sizeFromContents(QStyle::CT_ComboBox, &comboOption, contentSize(), &combo)
        );

        QSpinBox spin;
        style.polish(&spin);
        QStyleOptionSpinBox spinOption;
        spinOption.initFrom(&spin);
        spinOption.frame = true;
        spinOption.subControls = QStyle::SC_SpinBoxFrame | QStyle::SC_SpinBoxEditField
            | QStyle::SC_SpinBoxUp | QStyle::SC_SpinBoxDown;
        spinOption.buttonSymbols = QAbstractSpinBox::UpDownArrows;
        spinOption.rect = QRect(
            QPoint(),
            style.sizeFromContents(QStyle::CT_SpinBox, &spinOption, contentSize(), &spin)
        );

        QCOMPARE(comboOption.rect.width(), spinOption.rect.width());

        const QRect comboEdit = style.subControlRect(
            QStyle::CC_ComboBox,
            &comboOption,
            QStyle::SC_ComboBoxEditField,
            &combo
        );
        const QRect spinEdit
            = style.subControlRect(QStyle::CC_SpinBox, &spinOption, QStyle::SC_SpinBoxEditField, &spin);

        // Exactly the content it was sized for, not a pixel more: width reserved by a term
        // nothing lays out in reaches the editor as invisible slack.
        QCOMPARE(comboEdit.width(), contentWidth);
        QCOMPARE(comboEdit.right(), spinEdit.right());

        const QRect comboArrow
            = style.subControlRect(QStyle::CC_ComboBox, &comboOption, QStyle::SC_ComboBoxArrow, &combo);
        const QRect spinArrow
            = style.subControlRect(QStyle::CC_SpinBox, &spinOption, QStyle::SC_SpinBoxUp, &spin);

        QCOMPARE(comboArrow.width(), spinArrow.width());
        QCOMPARE(comboArrow.left(), spinArrow.left());
    }

    // The floor is the height token, applied to the widget rather than only to its hint.
    void test_aPolishedSpinBoxCarriesTheHeightTokenAsItsMinimum()  // NOLINT
    {
        const auto restore = overrideToken("FormControlMinHeight", "31px");

        Gui::FreeCADStyle freecadStyle;
        QStyle& style = freecadStyle;
        QSpinBox spin;
        style.polish(&spin);

        QCOMPARE(spin.minimumHeight(), 31);
    }

    // The floor follows the size variant the widget carries. Taking the plain one would hand a
    // compact control the standard height as a minimum, and grow it back to it.
    void test_theFloorFollowsTheControlSizeVariant()  // NOLINT
    {
        const auto restore = overrideToken("FormControlInternalMinHeight", "18px");

        Gui::FreeCADStyle freecadStyle;
        QStyle& style = freecadStyle;
        QSpinBox spin;
        spin.setProperty("controlSize", "internal");
        style.polish(&spin);

        QCOMPARE(spin.minimumHeight(), 18);
    }

    // What the floor is for. A layout given less room than it needs honours no minimum it was
    // told about, and takes the space back from its tallest items first — which is always the
    // inputs. Only the minimum on the widget itself survives that, because QWidget::setGeometry()
    // clamps against it.
    void test_aSqueezedLayoutLeavesASpinBoxAtItsMinimumHeight()  // NOLINT
    {
        Gui::FreeCADStyle freecadStyle;

        QWidget window;
        // A child rather than the window itself: a window is clamped to its own layout's
        // minimum, so it can never be short enough to squeeze what it holds.
        auto* page = new QWidget(&window);
        auto* layout = new QVBoxLayout(page);
        auto* spin = new QSpinBox;
        // Per widget: Qt hands a style down to children only through a stylesheet proxy, and in
        // the application this style is the application's own.
        spin->setStyle(&freecadStyle);
        layout->addWidget(spin);
        layout->addWidget(new QLabel(QStringLiteral("Font size")));

        window.resize(200, 200);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        const int needed = page->minimumSizeHint().height();
        page->setGeometry(0, 0, 200, needed - 10);

        QVERIFY2(
            page->height() < needed,
            "the page was not actually squeezed, so the test proves nothing"
        );
        QTRY_COMPARE(spin->height(), 26);
    }

    // The editor inside a spin box is placed by the spin box, inside a rect that already had the
    // control's padding taken out of it. A form control's floor there is taller than the room it
    // is given, and would push the editor out through the frame around it.
    void test_theEditorInsideASpinBoxTakesNoFloorOfItsOwn()  // NOLINT
    {
        Gui::FreeCADStyle freecadStyle;
        QStyle& style = freecadStyle;
        QSpinBox spin;

        auto* editor = spin.findChild<QLineEdit*>();
        QVERIFY(editor != nullptr);
        style.polish(editor);

        QCOMPARE(editor->minimumHeight(), 0);
    }

    // The floor belongs to the style, so it leaves with the style.
    void test_unpolishTakesTheFloorBackOff()  // NOLINT
    {
        Gui::FreeCADStyle freecadStyle;
        QStyle& style = freecadStyle;
        QSpinBox spin;

        style.polish(&spin);
        QCOMPARE(spin.minimumHeight(), 26);

        style.unpolish(&spin);
        QCOMPARE(spin.minimumHeight(), 0);
    }

    // A placement site that has asked for something taller has a reason the style does not know,
    // and polishing is not an occasion to overrule it.
    void test_aTallerMinimumSetOnTheWidgetSurvivesBothPasses()  // NOLINT
    {
        Gui::FreeCADStyle freecadStyle;
        QStyle& style = freecadStyle;
        QSpinBox spin;
        spin.setMinimumHeight(40);

        style.polish(&spin);
        QCOMPARE(spin.minimumHeight(), 40);

        style.unpolish(&spin);
        QCOMPARE(spin.minimumHeight(), 40);
    }
};

QTEST_MAIN(TestFormControlSizing)
#include "FormControlSizing.moc"
