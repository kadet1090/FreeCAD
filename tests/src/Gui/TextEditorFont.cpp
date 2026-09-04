// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QApplication>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QScopeGuard>
#include <QTest>

#include <App/Application.h>
#include <Base/Parameter.h>

#include <Gui/Application.h>
#include <Gui/FreeCADStyle.h>
#include <Gui/TextEdit.h>

#include "src/App/InitApplication.h"

// Exercises TextEditor::OnChange()'s FontSize/Font branch end to end: the Editor preference
// group in, the widget's style-override properties and resolved QFont out. The tab stop is the
// one part of this path that used to read a locally constructed QFont rather than the widget's
// own font() after the override landed; a regression back to that would leave the tab width
// stuck on the previous size while the glyphs themselves resized, so it is asserted here too.
class TestTextEditorFont: public QObject
{
    Q_OBJECT

public:
    TestTextEditorFont()
    {
        tests::initApplication();

        // Only one Gui::Application may exist per process; this suite is its own dedicated
        // executable (setup_qt_test), so constructing one here does not collide with any other
        // suite the way it would inside the shared Gui_tests_run gtest binary. GUIenabled=false
        // per the house fix for the same hazard elsewhere.
        if (Gui::Application::Instance == nullptr) {
            new Gui::Application(false);
        }
    }

private:
    // TextEditor reads Editor/Font, Editor/FontSize and Editor/TabSize from its own
    // WindowParameter("Editor") group, which is the same live user parameter group Preferences
    // -> Editor writes to, so a test touching it has to restore whatever was already there.
    [[nodiscard]] auto withEditorFont(const QString& family, int size, int tabSize) const
    {
        auto group = App::GetApplication().GetParameterGroupByPath(
            "User parameter:BaseApp/Preferences/Editor"
        );
        const std::string previousFamily = group->GetASCII("Font");
        const long previousSize = group->GetInt("FontSize", 10);
        const long previousTabSize = group->GetInt("TabSize", 4);

        group->SetASCII("Font", family.toStdString());
        group->SetInt("FontSize", size);
        group->SetInt("TabSize", tabSize);

        return qScopeGuard([group, previousFamily, previousSize, previousTabSize]() {
            group->SetASCII("Font", previousFamily);
            group->SetInt("FontSize", previousSize);
            group->SetInt("TabSize", previousTabSize);
        });
    }

    // FreeCADStyle must be the *application* style for a resolved QFont to reach font() at all.
    // Detaching the previous style's parent before swapping avoids Qt deleting it out from under
    // the next test slot, matching the idiom already established in StyleOverrides.cpp/
    // ReportOutputFont.cpp.
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
        const auto restoreEditorFont = withEditorFont(QStringLiteral("Fixture Mono"), 17, 4);

        Gui::TextEditor editor;

        QCOMPARE(
            editor.property("fcStyleTextEditFontFamily").toString(),
            QStringLiteral("'Fixture Mono'")
        );
        QCOMPARE(editor.property("fcStyleTextEditFontSize").toString(), QStringLiteral("17pt"));
        QCOMPARE(editor.font().family(), QStringLiteral("Fixture Mono"));
        QCOMPARE(editor.font().pointSize(), 17);
    }

    void test_anEmptyPreferenceFallsBackToTheSystemFixedFont()  // NOLINT
    {
        const auto styleGuard = installFreeCADStyle();
        const auto restoreEditorFont = withEditorFont(QString(), 10, 4);

        Gui::TextEditor editor;

        const QString systemFamily = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
        QCOMPARE(
            editor.property("fcStyleTextEditFontFamily").toString(),
            QStringLiteral("'%1'").arg(systemFamily)
        );
        QCOMPARE(editor.font().family(), systemFamily);
    }

    void test_theTabStopIsSizedFromTheOverriddenFontNotAStaleOne()  // NOLINT
    {
        const auto styleGuard = installFreeCADStyle();
        const auto restoreEditorFont = withEditorFont(QStringLiteral("Fixture Mono"), 24, 4);

        Gui::TextEditor editor;

        const QFontMetrics metric(editor.font());
        const int expectedTabStop = 4 * metric.horizontalAdvance(QLatin1Char('0'));
        QCOMPARE(editor.tabStopDistance(), static_cast<qreal>(expectedTabStop));
    }
};

QTEST_MAIN(TestTextEditorFont)

#include "TextEditorFont.moc"
