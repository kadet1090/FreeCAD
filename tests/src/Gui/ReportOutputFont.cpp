// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QApplication>
#include <QFontDatabase>
#include <QScopeGuard>
#include <QTest>

#include <App/Application.h>
#include <Base/Parameter.h>

#include <Gui/Application.h>
#include <Gui/FreeCADStyle.h>
#include <Gui/ReportView.h>

#include "src/App/InitApplication.h"

// Exercises ReportOutput::applyFontPreferences() end to end: the Editor preference group in,
// the widget's style-override properties and resolved QFont out. This is the one path that
// depends on the family reaching the parser as a *quoted* expression — an unquoted name would
// be silently discarded by the token system (no raw-text fallback for an override), and nothing
// short of resolving the font would catch that regression.
class TestReportOutputFont: public QObject
{
    Q_OBJECT

public:
    TestReportOutputFont()
    {
        tests::initApplication();

        // Only one Gui::Application may exist per process; this suite is its own dedicated
        // executable (setup_qt_test), so constructing one here does not collide with any other
        // suite the way it would inside the shared Gui_tests_run gtest binary. GUIenabled=false
        // per the house fix for the same hazard elsewhere: the connections that poison a later
        // headless suite live inside `if (GUIenabled)`, and nothing this test needs falls under
        // that flag.
        if (Gui::Application::Instance == nullptr) {
            new Gui::Application(false);
        }
    }

private:
    // ReportOutput reads Editor/Font and Editor/FontSize from the same live user parameter
    // group Preferences -> Editor writes to, so a test touching it has to restore whatever was
    // already there, not just clean up what it wrote.
    [[nodiscard]] auto withEditorFont(const QString& family, int size) const
    {
        auto group = App::GetApplication().GetParameterGroupByPath(
            "User parameter:BaseApp/Preferences/Editor"
        );
        const std::string previousFamily = group->GetASCII("Font");
        const long previousSize = group->GetInt("FontSize", 10);

        group->SetASCII("Font", family.toStdString());
        group->SetInt("FontSize", size);

        return qScopeGuard([group, previousFamily, previousSize]() {
            group->SetASCII("Font", previousFamily);
            group->SetInt("FontSize", previousSize);
        });
    }

    // FreeCADStyle must be the *application* style for a resolved QFont to reach font() at all
    // (ReportOutput::style() returns QApplication::style() until it has a styled ancestor).
    // Detaching the previous style's parent before swapping avoids Qt deleting it out from under
    // the next test slot, matching the idiom already established in StyleOverrides.cpp/
    // WidgetFonts.cpp.
    [[nodiscard]] static auto installFreeCADStyle()
    {
        QStyle* const previousStyle = QApplication::style();
        previousStyle->setParent(nullptr);
        QApplication::setStyle(new Gui::FreeCADStyle);

        return qScopeGuard([previousStyle]() { QApplication::setStyle(previousStyle); });
    }

private Q_SLOTS:
    void test_theOverrideCarriesThePreferredFamilyAndSize()  // NOLINT
    {
        const auto styleGuard = installFreeCADStyle();
        const auto restoreEditorFont = withEditorFont(QStringLiteral("Fixture Mono"), 17);

        Gui::DockWnd::ReportOutput report;

        QCOMPARE(
            report.property("fcStyleTextEditFontFamily").toString(),
            QStringLiteral("'Fixture Mono'")
        );
        QCOMPARE(report.property("fcStyleTextEditFontSize").toString(), QStringLiteral("17pt"));
        QCOMPARE(report.font().family(), QStringLiteral("Fixture Mono"));
        QCOMPARE(report.font().pointSize(), 17);
    }

    void test_anEmptyPreferenceFallsBackToTheSystemFixedFont()  // NOLINT
    {
        const auto styleGuard = installFreeCADStyle();
        const auto restoreEditorFont = withEditorFont(QString(), 10);

        Gui::DockWnd::ReportOutput report;

        const QString systemFamily = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
        QCOMPARE(
            report.property("fcStyleTextEditFontFamily").toString(),
            QStringLiteral("'%1'").arg(systemFamily)
        );
        QCOMPARE(report.font().family(), systemFamily);
    }
};

QTEST_MAIN(TestReportOutputFont)

#include "ReportOutputFont.moc"
