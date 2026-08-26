// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QTest>
#include <QWidget>

#include "src/App/InitApplication.h"

#include <Gui/Application.h>
#include <Gui/FreeCADStyle.h>
#include <Gui/StyleParameters/ParameterManager.h>

class TestStyleTransparency: public QObject
{
    Q_OBJECT

public:
    TestStyleTransparency()
    {
        tests::initApplication();

        if (Gui::Application::Instance == nullptr) {
            new Gui::Application(true);
        }

        // Attached to a plain QWidget through the "component" property, so these tokens
        // apply without needing a real QListView or QTreeView.
        Gui::Application::Instance->styleParameterManager()->addSource(
            new Gui::StyleParameters::InMemoryParameterSource(
                {
                    {.name = "ListIsTransparent", .value = "false"},
                    {.name = "TreeIsTransparent", .value = "true"},
                    // The short form applies in both states, the qualified one only when the
                    // widget is itself painted over a transparent surface.
                    {.name = "TestPanelIsTransparent", .value = "true"},
                    {.name = "TestPanelTransparentIsTransparent", .value = "false"},
                    // reset() yields None, which resolves to nothing — the token is cancelled
                    // and the surface passes through unchanged.
                    {.name = "TestResetIsTransparent", .value = "reset()"},
                    // A Numeric is not a boolean, so a strictly typed read rejects it and the
                    // surface passes through unchanged.
                    {.name = "TestNumericIsTransparent", .value = "1"},
                    // Mirrors the production token: the property editor paints an opaque
                    // panel, so it presents an opaque surface to everything inside it.
                    {.name = "PropertyEditorTransparentIsTransparent", .value = "false"},
                },
                {.name = "Transparency Fixture"}
            )
        );
    }

private Q_SLOTS:

    // A widget that declares IsTransparent=false still renders transparent itself;
    // only what it presents downward changes.
    void test_tokenBreaksChainBelowButNotForItself()  // NOLINT
    {
        QWidget root;
        auto* passthrough = new QWidget(&root);
        auto* breaker = new QWidget(passthrough);
        breaker->setProperty("component", "List");
        auto* child = new QWidget(breaker);

        Gui::FreeCADStyle style;
        style.updateTransparency(&root, true);

        QVERIFY(Gui::FreeCADStyle::isTransparent(&root));
        QVERIFY(Gui::FreeCADStyle::isTransparent(passthrough));
        QVERIFY(Gui::FreeCADStyle::isTransparent(breaker));
        QVERIFY(!Gui::FreeCADStyle::isTransparent(child));
    }

    // IsTransparent=true opens a root for the subtree, not for the declaring widget.
    void test_tokenOpensRootForSubtreeOnly()  // NOLINT
    {
        QWidget root;
        auto* opener = new QWidget(&root);
        opener->setProperty("component", "Tree");
        auto* child = new QWidget(opener);

        Gui::FreeCADStyle style;
        style.updateTransparency(&root, false);

        QVERIFY(!Gui::FreeCADStyle::isTransparent(&root));
        QVERIFY(!Gui::FreeCADStyle::isTransparent(opener));
        QVERIFY(Gui::FreeCADStyle::isTransparent(child));
    }

    // The property, unlike the token, seeds the carrying widget itself.
    void test_propertySeedsWidgetItself()  // NOLINT
    {
        QWidget root;
        auto* panel = new QWidget(&root);
        panel->setProperty("transparent", true);
        auto* child = new QWidget(panel);

        Gui::FreeCADStyle style;
        style.updateTransparency(&root, false);

        QVERIFY(!Gui::FreeCADStyle::isTransparent(&root));
        QVERIFY(Gui::FreeCADStyle::isTransparent(panel));
        QVERIFY(Gui::FreeCADStyle::isTransparent(child));
    }

    void test_flippingPropertyRepropagates()  // NOLINT
    {
        QWidget root;
        auto* panel = new QWidget(&root);
        panel->setProperty("transparent", true);
        auto* child = new QWidget(panel);

        Gui::FreeCADStyle style;
        style.updateTransparency(&root, false);
        QVERIFY(Gui::FreeCADStyle::isTransparent(child));

        panel->setProperty("transparent", false);
        style.updateTransparency(&root, false);

        QVERIFY(!Gui::FreeCADStyle::isTransparent(panel));
        QVERIFY(!Gui::FreeCADStyle::isTransparent(child));
    }

    // polish() must seed a widget from what its parent presents downward
    // (transparencyBelow()), not from what the parent renders with (isTransparent()) —
    // the two disagree here precisely because breaker is a chain-breaker.
    void test_polishSeedsFromTransparencyBelowNotIsTransparent()  // NOLINT
    {
        QWidget root;
        auto* breaker = new QWidget(&root);
        breaker->setProperty("component", "List");

        Gui::FreeCADStyle style;
        style.updateTransparency(&root, true);
        QVERIFY(Gui::FreeCADStyle::isTransparent(breaker));

        // Simulate a widget built after the subtree was propagated — e.g. a lazily created
        // editor — whose only transparency signal comes from polish().
        auto* child = new QWidget(breaker);
        style.polish(child);

        QVERIFY(!Gui::FreeCADStyle::isTransparent(child));
    }

    // Regression guard: an explicit "transparent" property must win in polish() exactly as it
    // already does in updateTransparency() — the two must share one implementation of a
    // widget's own seed, or one of them can silently forget the override.
    void test_polishHonoursOverrideProperty()  // NOLINT
    {
        QWidget root;
        auto* panel = new QWidget(&root);
        panel->setProperty("transparent", true);

        Gui::FreeCADStyle style;
        style.polish(panel);

        QVERIFY(Gui::FreeCADStyle::isTransparent(panel));
    }

    // A popup, menu, tooltip or dialog is a separate top-level surface over the desktop and
    // must not inherit through the QObject parent/child link used only for lifetime management,
    // even when polish() reaches it directly instead of through the recursive propagator.
    void test_polishDoesNotInheritForWindowChild()  // NOLINT
    {
        QWidget root;
        root.setProperty("transparent", true);

        Gui::FreeCADStyle style;
        style.updateTransparency(&root, false);
        QVERIFY(Gui::FreeCADStyle::isTransparent(&root));

        // Simulate a popup created after propagation — e.g. on first show — whose only
        // transparency signal comes from polish().
        auto* popup = new QWidget(&root, Qt::Popup);
        style.polish(popup);

        QVERIFY(!Gui::FreeCADStyle::isTransparent(popup));
    }

    // The tag exists only to select the Transparent variant during token resolution; without
    // that link every other test here would still pass while nothing painted differently.
    void test_tagSelectsTransparencyVariant()  // NOLINT
    {
        using Gui::StyleParameters::TransparencyMode;
        using Gui::StyleParameters::VariantSlot;

        QWidget root;
        auto* panel = new QWidget(&root);
        panel->setProperty("transparent", true);
        auto* opaque = new QWidget(&root);

        Gui::FreeCADStyle style;
        style.updateTransparency(&root, false);

        const auto transparentContext = Gui::FreeCADStyle::contextOf(panel);
        const auto opaqueContext = Gui::FreeCADStyle::contextOf(opaque);

        QCOMPARE(
            transparentContext.variant.get(VariantSlot::TransparencyMode),
            static_cast<uint8_t>(TransparencyMode::Transparent)
        );
        QCOMPARE(
            opaqueContext.variant.get(VariantSlot::TransparencyMode),
            static_cast<uint8_t>(TransparencyMode::Normal)
        );
    }

    // The unqualified token applies wherever the component appears, so an opaque parent still
    // sees it and the subtree below opens.
    void test_unqualifiedTokenAppliesOverOpaqueSurface()  // NOLINT
    {
        QWidget root;
        auto* panel = new QWidget(&root);
        panel->setProperty("component", "TestPanel");
        auto* child = new QWidget(panel);

        Gui::FreeCADStyle style;
        style.updateTransparency(&root, false);

        QVERIFY(!Gui::FreeCADStyle::isTransparent(panel));
        QVERIFY(Gui::FreeCADStyle::isTransparent(child));
    }

    // The qualified token is reachable only once the widget's own tag has selected the
    // Transparent variant, so it must win over the unqualified one here — which it can only
    // do if the tag is written before the component's own IsTransparent is resolved.
    void test_qualifiedTokenWinsOverTransparentSurface()  // NOLINT
    {
        QWidget root;
        auto* panel = new QWidget(&root);
        panel->setProperty("component", "TestPanel");
        auto* child = new QWidget(panel);

        Gui::FreeCADStyle style;
        style.updateTransparency(&root, true);

        QVERIFY(Gui::FreeCADStyle::isTransparent(panel));
        QVERIFY(!Gui::FreeCADStyle::isTransparent(child));
    }

    // reset() cancels an inherited token, leaving the surface to pass through untouched.
    void test_resetTokenPassesSurfaceThrough()  // NOLINT
    {
        QWidget transparentRoot;
        auto* transparentPanel = new QWidget(&transparentRoot);
        transparentPanel->setProperty("component", "TestReset");
        auto* transparentChild = new QWidget(transparentPanel);

        QWidget opaqueRoot;
        auto* opaquePanel = new QWidget(&opaqueRoot);
        opaquePanel->setProperty("component", "TestReset");
        auto* opaqueChild = new QWidget(opaquePanel);

        Gui::FreeCADStyle style;
        style.updateTransparency(&transparentRoot, true);
        style.updateTransparency(&opaqueRoot, false);

        QVERIFY(Gui::FreeCADStyle::isTransparent(transparentChild));
        QVERIFY(!Gui::FreeCADStyle::isTransparent(opaqueChild));
    }

    // A non-boolean token is rejected rather than coerced, so it too passes the surface
    // through instead of silently reading as true.
    void test_nonBooleanTokenPassesSurfaceThrough()  // NOLINT
    {
        QWidget transparentRoot;
        auto* transparentPanel = new QWidget(&transparentRoot);
        transparentPanel->setProperty("component", "TestNumeric");
        auto* transparentChild = new QWidget(transparentPanel);

        QWidget opaqueRoot;
        auto* opaquePanel = new QWidget(&opaqueRoot);
        opaquePanel->setProperty("component", "TestNumeric");
        auto* opaqueChild = new QWidget(opaquePanel);

        Gui::FreeCADStyle style;
        style.updateTransparency(&transparentRoot, true);
        style.updateTransparency(&opaqueRoot, false);

        QVERIFY(Gui::FreeCADStyle::isTransparent(transparentChild));
        QVERIFY(!Gui::FreeCADStyle::isTransparent(opaqueChild));
    }

    // An unregistered component override participates in the fallback chain exactly like a
    // registered component, so PropertyEditor can stop the chain without a StyleComponent value.
    void test_componentOverrideBreaksChainBelow()  // NOLINT
    {
        QWidget root;
        auto* editor = new QWidget(&root);
        editor->setProperty("component", "PropertyEditor");
        auto* viewport = new QWidget(editor);

        Gui::FreeCADStyle style;
        style.updateTransparency(&root, true);

        QVERIFY(Gui::FreeCADStyle::isTransparent(editor));
        QVERIFY(!Gui::FreeCADStyle::isTransparent(viewport));
    }
};

QTEST_MAIN(TestStyleTransparency)

#include "StyleTransparency.moc"
