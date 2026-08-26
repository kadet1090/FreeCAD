// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QToolButton>
#include <QTest>

#include "src/App/InitApplication.h"

#include <Gui/Application.h>
#include <Gui/InputField.h>
#include <Gui/Widgets.h>

// The expression button is fixed to an icon-sized square, but as a QToolButton its size hint
// carries the height every form control is asked for. Placing it from the hint instead of its
// real geometry lifts it out of the field it overlays.
class TestExpressionButtonLayout: public QObject
{
    Q_OBJECT

public:
    TestExpressionButtonLayout()
    {
        tests::initApplication();

        // The button reads its fixed size from the style parameters at construction.
        if (Gui::Application::Instance == nullptr) {
            new Gui::Application(true);
        }
    }

private:
    // Reaches the space reservation, which production code runs from bind() and so behind a
    // document object no unit test has.
    class ProbeLineEdit: public Gui::ExpLineEdit
    {
    public:
        using Gui::ExpLineEdit::ExpLineEdit;

        void reserve()
        {
            reserveIconSpace(this);
        }
    };

    // Resizes the field and delivers the event, which Qt otherwise withholds from a widget that
    // was never shown.
    static void resizeField(QWidget& field, QSize size)
    {
        const QSize previous = field.size();
        field.resize(size);

        QResizeEvent event(size, previous);
        QCoreApplication::sendEvent(&field, &event);
    }

    // Fails the calling test if the field's overlaid button is not wholly inside it and centred
    // on its vertical axis.
    static void verifyIconSitsInField(const QWidget& field)
    {
        const auto* icon = field.findChild<QToolButton*>();
        QVERIFY(icon != nullptr);

        const QRect iconRect = icon->geometry();
        QVERIFY2(
            field.rect().contains(iconRect),
            qPrintable(QStringLiteral("icon %1 escapes field %2")
                           .arg(QDebug::toString(iconRect), QDebug::toString(field.rect())))
        );
        QCOMPARE(iconRect.center().y(), field.rect().center().y());
        QVERIFY(iconRect.right() < field.rect().right());
    }

private Q_SLOTS:

    // The string rows of the property editor, where the misplacement was reported.
    void test_expressionLineEditCentresItsIcon()  // NOLINT
    {
        Gui::ExpLineEdit field;
        resizeField(field, {200, 24});

        verifyIconSitsInField(field);
    }

    // A taller field must not leave the icon pinned to the top either.
    void test_expressionLineEditRecentresOnResize()  // NOLINT
    {
        Gui::ExpLineEdit field;
        resizeField(field, {200, 24});
        resizeField(field, {200, 40});

        verifyIconSitsInField(field);
    }

    // Same button, same placement path, reached through the validation indicator instead.
    void test_inputFieldCentresItsIcon()  // NOLINT
    {
        Gui::InputField field;
        resizeField(field, {200, 24});

        verifyIconSitsInField(field);
    }

    // A line edit paints its panel over its contents rect, so reserving room for the button by
    // insetting that rect withdraws the field's background from under the button. The room has
    // to come out of the text margins instead.
    void test_reservedSpaceLeavesTheFieldPanelWhole()  // NOLINT
    {
        ProbeLineEdit field;
        resizeField(field, {200, 24});
        field.reserve();

        const auto* icon = field.findChild<QToolButton*>();
        QVERIFY(icon != nullptr);

        QCOMPARE(field.contentsRect(), field.rect());
        QVERIFY(field.textMargins().right() >= icon->width());
    }
};

QTEST_MAIN(TestExpressionButtonLayout)

#include "ExpressionButtonLayout.moc"
