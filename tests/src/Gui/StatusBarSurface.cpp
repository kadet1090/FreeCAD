// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QStatusBar>
#include <QStyleOption>
#include <QTest>
#include <QWidget>

#include "src/App/InitApplication.h"

#include <Gui/Application.h>
#include <Gui/FCStatusBar.h>
#include <Gui/FreeCADStyle.h>
#include <Gui/StyleParameters/ParameterManager.h>

namespace
{

// The fixture's own numbers, chosen so no two sums collide — a wrong token therefore fails
// loudly instead of coincidentally matching. The edge is 2px rather than 1px so a pixel
// sampled on it sits solidly inside the border rather than on a boundary.
constexpr int statusBarEdge = 2;
constexpr int statusBarFloor = 31;

const QColor statusBarBackground = QColor(QStringLiteral("#202020"));
const QColor statusBarEdgeColor = QColor(QStringLiteral("#00ff00"));

const QColor messageColor = QColor(QStringLiteral("#0000ff"));
const QColor warningColor = QColor(QStringLiteral("#ffff00"));
const QColor errorColor = QColor(QStringLiteral("#ff0000"));
const QColor fallbackColor = QColor(QStringLiteral("#123456"));

}  // namespace

class TestStatusBarSurface: public QObject
{
    Q_OBJECT

public:
    TestStatusBarSurface()
    {
        tests::initApplication();

        if (Gui::Application::Instance == nullptr) {
            new Gui::Application(true);
        }

        Gui::Application::Instance->styleParameterManager()->addSource(
            new Gui::StyleParameters::InMemoryParameterSource(
                {
                    {.name = "StatusBarBackground", .value = "#202020"},
                    {.name = "StatusBarBorderColor", .value = "#00ff00"},
                    {.name = "StatusBarBorderThickness", .value = "border_thickness(0px, top: 2px)"},
                    {.name = "StatusBarMinHeight", .value = "31px"},
                    {.name = "StatusBarMessageTextColor", .value = "#0000ff"},
                    {.name = "StatusBarMessageWarningTextColor", .value = "#ffff00"},
                    {.name = "StatusBarMessageErrorTextColor", .value = "#ff0000"},
                },
                {.name = "Status Bar Surface"}
            )
        );
    }

private:
    // Paints the bar panel over magenta and hands back the canvas, so a test can ask both what
    // was painted and whether anything was.
    static QImage paintedPanel(QStyle& style, QWidget* widget)
    {
        QStyleOption option;
        option.initFrom(widget);
        option.rect = QRect(0, 0, 80, 24);

        QImage canvas(option.rect.size(), QImage::Format_ARGB32);
        canvas.fill(Qt::magenta);

        QPainter painter(&canvas);
        style.drawPrimitive(QStyle::PE_PanelStatusBar, &option, &painter, widget);
        painter.end();

        return canvas;
    }

    // Whether anything on the canvas was painted in exactly @p color. Glyph cores are solid at
    // this font size, so an antialiased edge never has to be the thing under test.
    static bool containsColor(const QImage& canvas, const QColor& color)
    {
        for (int row = 0; row < canvas.height(); ++row) {
            for (int column = 0; column < canvas.width(); ++column) {
                if (canvas.pixelColor(column, row) == color) {
                    return true;
                }
            }
        }

        return false;
    }

    // A bar wide enough for a message, with one permanent item so messageRect() has a bound to
    // find, rendered at a font size whose strokes are solid.
    static QImage renderedBar(Gui::FCStatusBar& bar, Gui::StyleParameters::MessageLevel level)
    {
        bar.setMessageLevel(level);
        bar.showMessage(QStringLiteral("Recompute failed"));

        QImage canvas(bar.size(), QImage::Format_ARGB32);
        canvas.fill(Qt::magenta);
        bar.render(&canvas, QPoint(), QRegion(), QWidget::DrawWindowBackground);

        return canvas;
    }

private Q_SLOTS:
    void test_theSurfaceIsPaintedFromTheStatusBarTokens()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QStatusBar bar;

        const QImage canvas = paintedPanel(style, &bar);

        QCOMPARE(canvas.pixelColor(40, 12), statusBarBackground);
        QCOMPARE(canvas.pixelColor(40, 0), statusBarEdgeColor);
        QCOMPARE(canvas.pixelColor(40, statusBarEdge), statusBarBackground);
    }

    void test_aBarWithNoStatedSurfaceKeepsTheBaseStyles()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QStatusBar bar;
        bar.setStyle(&style);
        style.polish(&bar);

        // An override of "reset()" is how a theme states nothing at all for a token, so this is
        // the same silence a theme without a StatusBar block leaves. Polished first: an override
        // reaches the resolver through the set stored on the widget, which polish() is what puts
        // there.
        Gui::FreeCADStyle::setStyleOverride(
            &bar,
            QStringLiteral("StatusBarBackground"),
            QStringLiteral("reset()")
        );

        const QImage canvas = paintedPanel(style, &bar);

        // Fusion paints nothing for this primitive, so declining leaves the canvas as it was.
        // The edge is stated on its own token and untouched by this override, so sampling it too
        // is what tells a full decline apart from one that merely skipped the fill: painting the
        // border alone would leave this pixel green instead.
        QCOMPARE(canvas.pixelColor(40, 12), QColor(Qt::magenta));
        QCOMPARE(canvas.pixelColor(40, 0), QColor(Qt::magenta));
    }

    void test_theBarKeepsTheStatedHeightFloor()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QStatusBar bar;

        style.polish(&bar);

        QCOMPARE(bar.minimumHeight(), statusBarFloor);
    }

    void test_eachLevelResolvesItsOwnColour()  // NOLINT
    {
        using Gui::StyleParameters::MessageLevel;

        Gui::FreeCADStyle style;
        QStatusBar bar;
        bar.setStyle(&style);

        QCOMPARE(
            Gui::FreeCADStyle::statusMessageColor(&bar, MessageLevel::Default, fallbackColor),
            messageColor
        );
        QCOMPARE(
            Gui::FreeCADStyle::statusMessageColor(&bar, MessageLevel::Warning, fallbackColor),
            warningColor
        );
        QCOMPARE(
            Gui::FreeCADStyle::statusMessageColor(&bar, MessageLevel::Error, fallbackColor),
            errorColor
        );
    }

    void test_aLevelWithNoColourOfItsOwnFallsBackToTheMessageToken()  // NOLINT
    {
        using Gui::StyleParameters::MessageLevel;

        Gui::FreeCADStyle style;
        QStatusBar bar;
        bar.setStyle(&style);

        // The fixture states no Critical colour, so the severity variant has to fall through to
        // the unqualified message token rather than to the caller's fallback.
        QCOMPARE(
            Gui::FreeCADStyle::statusMessageColor(&bar, MessageLevel::Critical, fallbackColor),
            messageColor
        );
    }

    void test_aStatedOverrideOutranksTheThemeColour()  // NOLINT
    {
        using Gui::StyleParameters::MessageLevel;

        Gui::FreeCADStyle style;
        QStatusBar bar;
        bar.setStyle(&style);
        style.polish(&bar);

        // Exactly the call StatusBarObserver makes when the user has set an OutputWindow colour.
        Gui::FreeCADStyle::setStyleOverride(
            &bar,
            QStringLiteral("StatusBarMessageErrorTextColor"),
            QStringLiteral("#00ffff")
        );

        QCOMPARE(
            Gui::FreeCADStyle::statusMessageColor(&bar, MessageLevel::Error, fallbackColor),
            QColor(QStringLiteral("#00ffff"))
        );
    }

    void test_theBarPaintsItsOwnSurface()  // NOLINT
    {
        Gui::FreeCADStyle style;
        Gui::FCStatusBar bar;
        bar.setStyle(&style);
        bar.resize(200, 30);

        QImage canvas(bar.size(), QImage::Format_ARGB32);
        canvas.fill(Qt::magenta);
        bar.render(&canvas, QPoint(), QRegion(), QWidget::DrawWindowBackground);

        QCOMPARE(canvas.pixelColor(100, 15), statusBarBackground);
        QCOMPARE(canvas.pixelColor(100, 0), statusBarEdgeColor);
    }

    void test_theMessageIsPaintedInItsLevelsColour()  // NOLINT
    {
        using Gui::StyleParameters::MessageLevel;

        Gui::FreeCADStyle style;
        Gui::FCStatusBar bar;
        bar.setStyle(&style);
        bar.setFont(QFont(bar.font().family(), 20));
        bar.resize(400, 40);
        bar.addPermanentWidget(new QLabel(QStringLiteral("mm"), &bar));

        QVERIFY(containsColor(renderedBar(bar, MessageLevel::Error), errorColor));
        QVERIFY(containsColor(renderedBar(bar, MessageLevel::Warning), warningColor));
    }

    void test_theLevelChangesWhatIsPainted()  // NOLINT
    {
        using Gui::StyleParameters::MessageLevel;

        Gui::FreeCADStyle style;
        Gui::FCStatusBar bar;
        bar.setStyle(&style);
        bar.setFont(QFont(bar.font().family(), 20));
        bar.resize(400, 40);

        // The same message twice: only the level differs, so a bar that ignored it would paint
        // the identical image both times.
        QVERIFY(renderedBar(bar, MessageLevel::Error) != renderedBar(bar, MessageLevel::Default));
    }
};

QTEST_MAIN(TestStatusBarSurface)
#include "StatusBarSurface.moc"
