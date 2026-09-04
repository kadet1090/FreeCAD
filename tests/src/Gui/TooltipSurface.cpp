// SPDX-License-Identifier: LGPL-2.1-or-later

#include <memory>

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QStyleOptionFrame>
#include <QTest>

#include "src/App/InitApplication.h"

#include <Gui/Application.h>
#include <Gui/FreeCADStyle.h>
#include <Gui/NotificationBox.h>
#include <Gui/StyleParameters/ParameterManager.h>

// The fixture's own numbers, chosen so no two sums collide — a wrong token therefore fails
// loudly instead of coincidentally matching. The border is 2px rather than 1px so a pixel
// sampled on the panel's edge sits solidly inside the border ring rather than on a boundary
// an antialiased path could soften.
constexpr int tooltipBorder = 2;
constexpr int tooltipPadding = 7;
constexpr int tooltipFontSize = 17;
constexpr int tooltipFontWeight = 700;
constexpr int notificationPadding = 11;

// QTipLabel adds one pixel to the metric before using it as the label's margin, and one
// further pixel on the left through setIndent(1). Both are Qt's arithmetic, not ours.
constexpr int qtMarginConstant = 1;
constexpr int qtLeftIndent = 1;

const QColor tooltipBackground = QColor(QStringLiteral("#202020"));
const QColor tooltipBorderColor = QColor(QStringLiteral("#00ff00"));

const QColor notificationBorderColor = QColor(QStringLiteral("#ff00ff"));

class TestTooltipSurface: public QObject
{
    Q_OBJECT

public:
    TestTooltipSurface()
    {
        tests::initApplication();

        if (Gui::Application::Instance == nullptr) {
            new Gui::Application(true);
        }

        Gui::Application::Instance->styleParameterManager()->addSource(
            new Gui::StyleParameters::InMemoryParameterSource(
                {
                    {.name = "TooltipBackground", .value = "#202020"},
                    {.name = "TooltipBorderColor", .value = "#00ff00"},
                    {.name = "TooltipBorderThickness", .value = "2px"},
                    {.name = "TooltipBorderRadius", .value = "0px"},
                    {.name = "TooltipPadding", .value = "7px"},
                    {.name = "TooltipFontSize", .value = "17px"},
                    {.name = "TooltipFontWeight", .value = "700"},
                    {.name = "TooltipFontFamily", .value = "'Fixture Tip Sans'"},

                    // Only these two on the Notification namespace. Everything else a
                    // notification resolves has to come from Tooltip*, which is what makes
                    // the fallback visible in the numbers rather than merely assumed.
                    {.name = "NotificationPadding", .value = "11px"},
                    {.name = "NotificationBorderColor", .value = "#ff00ff"},
                },
                {.name = "Tooltip Surface"}
            )
        );
    }

private:
    // A stand-in for QTipLabel: Qt's own class is private and Gui::NotificationLabel is local
    // to NotificationBox.cpp. Both are QLabels carrying the Qt::ToolTip window type, which is
    // the whole of what FreeCADStyle::isTooltipLabel() tests.
    static std::unique_ptr<QLabel> tooltipLabel()
    {
        QWidget* noParent = nullptr;

        auto label = std::make_unique<QLabel>(noParent, Qt::ToolTip);
        label->setText(QStringLiteral("Create a new sketch"));
        label->setAlignment(Qt::AlignLeft);

        return label;
    }

    // Paints the tip panel over magenta and hands back the canvas, so a test can ask both
    // what was painted and whether anything was.
    static QImage paintedPanel(QStyle& style, QWidget* widget)
    {
        QStyleOptionFrame option;
        option.initFrom(widget);
        option.rect = QRect(0, 0, 60, 24);

        QImage canvas(option.rect.size(), QImage::Format_ARGB32);
        canvas.fill(Qt::magenta);

        QPainter painter(&canvas);
        style.drawPrimitive(QStyle::PE_PanelTipLabel, &option, &painter, widget);
        painter.end();

        return canvas;
    }

private Q_SLOTS:
    void test_theSurfaceIsPaintedFromTheTooltipTokens()  // NOLINT
    {
        Gui::FreeCADStyle style;
        const auto label = tooltipLabel();

        const QImage canvas = paintedPanel(style, label.get());

        QCOMPARE(canvas.pixelColor(30, 12), tooltipBackground);
        QCOMPARE(canvas.pixelColor(30, 0), tooltipBorderColor);
    }

    // Declining a widget means falling through to the base style, not painting nothing: a tip
    // panel that leaves the canvas untouched is a hole, not a decline.
    void test_aPlainLabelKeepsTheBaseStylePanel()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QLabel plain;

        const QImage canvas = paintedPanel(style, &plain);

        QVERIFY(canvas.pixelColor(30, 12) != tooltipBackground);
        QVERIFY(canvas.pixelColor(30, 12) != QColor(Qt::magenta));
    }

    // The metric is not the inset: QTipLabel's constructor adds a pixel to it before using it
    // as the label's margin. Reconstructing that arithmetic and measuring the size hint it
    // produces is what says the token reached the text, rather than only that pixelMetric
    // returned some number.
    void test_theContentInsetIsTheBorderPlusThePadding()  // NOLINT
    {
        Gui::FreeCADStyle style;
        const auto label = tooltipLabel();

        const int metric = style.pixelMetric(QStyle::PM_ToolTipLabelFrameWidth, nullptr, label.get());

        label->setMargin(0);
        label->setIndent(0);
        const QSize bare = label->sizeHint();

        // The two lines QTipLabel's constructor runs after ensurePolished().
        label->setMargin(qtMarginConstant + metric);
        label->setIndent(qtLeftIndent);
        const QSize inset = label->sizeHint();

        const int expected = tooltipBorder + tooltipPadding;

        QCOMPARE(inset.height() - bare.height(), 2 * expected);
        QCOMPARE(inset.width() - bare.width(), (2 * expected) + qtLeftIndent);
    }

    // A metric this style does not own has to reach the base style, never a fabricated value.
    void test_aPlainLabelKeepsTheBaseStyleInset()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QLabel plain;

        QCOMPARE(
            style.pixelMetric(QStyle::PM_ToolTipLabelFrameWidth, nullptr, &plain),
            style.baseStyle()->pixelMetric(QStyle::PM_ToolTipLabelFrameWidth, nullptr, &plain)
        );
    }

    // Qt takes a tooltip's font from QApplication::font("QTipLabel"), never from the style, so
    // polish() is the only place a FontSize token can reach the label.
    void test_polishAppliesTheTooltipFontTokens()  // NOLINT
    {
        Gui::FreeCADStyle style;
        const auto label = tooltipLabel();

        style.polish(label.get());

        QCOMPARE(label->font().pixelSize(), tooltipFontSize);
        QCOMPARE(static_cast<int>(label->font().weight()), tooltipFontWeight);
        QCOMPARE(label->font().families(), QStringList({QStringLiteral("Fixture Tip Sans")}));
    }

    // The property has to be on the label NotificationBox builds. A test that tags its own
    // widget proves only that overrides work at all, which the slot below already covers.
    void test_theNotificationLabelCarriesTheOverride()  // NOLINT
    {
        QVERIFY(Gui::NotificationBox::showText(QPoint(0, 0), QStringLiteral("Document saved")));

        QWidget* notification = nullptr;
        for (QWidget* candidate : QApplication::topLevelWidgets()) {
            if (candidate->objectName() == QLatin1String("NotificationBox_label")) {
                notification = candidate;
            }
        }

        QVERIFY(notification != nullptr);
        QCOMPARE(notification->property("component").toString(), QStringLiteral("Notification"));

        Gui::NotificationBox::hideText();
    }

    // One number carries both halves of the override: the padding comes from Notification*,
    // and the border it is added to is not stated there, so it has to fall through to
    // Tooltip*. The two sampled pixels say the same thing about the painted surface.
    void test_aNotificationOverridesPaddingAndFallsBackForTheRest()  // NOLINT
    {
        Gui::FreeCADStyle style;
        const auto label = tooltipLabel();
        label->setProperty("component", "Notification");

        QCOMPARE(
            style.pixelMetric(QStyle::PM_ToolTipLabelFrameWidth, nullptr, label.get()),
            tooltipBorder + notificationPadding - qtMarginConstant
        );

        const QImage canvas = paintedPanel(style, label.get());

        QCOMPARE(canvas.pixelColor(30, 0), notificationBorderColor);
        QCOMPARE(canvas.pixelColor(30, 12), tooltipBackground);
    }
};

QTEST_MAIN(TestTooltipSurface)
#include "TooltipSurface.moc"
