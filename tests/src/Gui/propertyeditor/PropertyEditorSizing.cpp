// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QScrollBar>
#include <QTest>
#include <QWidget>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>

#include <Gui/Application.h>
#include <Gui/FreeCADStyle.h>
#include <Gui/PropertyView.h>
#include <Gui/SoFCDB.h>
#include <Gui/propertyeditor/PropertyEditor.h>

#include <src/App/InitApplication.h>

using Gui::PropertyEditor::PropertyEditor;

class TestPropertyEditorSizing: public QObject
{
    Q_OBJECT

public:
    TestPropertyEditorSizing()
    {
        tests::initApplication();

        if (Gui::Application::Instance == nullptr) {
            new Gui::Application(true);
        }

        // Property items register themselves through the same call the real startup process
        // uses; without it the model can't turn the test object's properties into rows.
        if (!Gui::SoFCDB::isInitialized()) {
            Gui::SoFCDB::init();
        }
    }

private Q_SLOTS:

    void initTestCase()  // NOLINT
    {
        // Keep this fixture headless: a view-backed document would stand up a 3D view and
        // its Coin scene graph, which this offscreen test harness cannot support.
        App::DocumentInitFlags createFlags;
        createFlags.createView = false;
        document = App::GetApplication().newDocument("PropertyEditorSizing", nullptr, createFlags);
        object = document->addObject("App::FeatureTest", "Subject");
        QVERIFY(object != nullptr);
    }

    void cleanupTestCase()  // NOLINT
    {
        App::GetApplication().closeDocument("PropertyEditorSizing");
    }

    // The editor carries the token namespace the transparent panel styling resolves through.
    void test_declaresPropertyEditorComponent()  // NOLINT
    {
        PropertyEditor editor;

        QCOMPARE(editor.property("component").toString(), QStringLiteral("PropertyEditor"));
    }

    // An empty model needs no panel at all, so nothing is painted with nothing selected.
    void test_emptyModelHasNoContentHeight()  // NOLINT
    {
        PropertyEditor editor;

        QCOMPARE(editor.contentHeight(), 0);
    }

    // Content height must reflect what is actually on screen: once populated it covers more
    // than a single row plus the frame, and collapsing rows away shrinks it again.
    void test_populatedModelReportsMeaningfulHeight()  // NOLINT
    {
        PropertyEditor editor;
        buildUpSubject(editor);
        editor.expandAll();

        QVERIFY(editor.model()->rowCount() > 0);
        int expandedHeight = editor.contentHeight();
        QVERIFY(expandedHeight > 2 * editor.frameWidth());

        editor.collapseAll();
        int collapsedHeight = editor.contentHeight();

        QVERIFY(collapsedHeight > 0);
        QVERIFY(collapsedHeight < expandedHeight);
    }

    // Content height must not depend on how far the view happens to be scrolled — only on
    // what rows exist and are expanded — or a docked, scrolled editor would report the wrong
    // size the moment it becomes transparent.
    void test_contentHeightIsIndependentOfScrollPosition()  // NOLINT
    {
        PropertyEditor editor;
        buildUpSubject(editor);
        editor.expandAll();
        editor.resize(300, 100);

        int heightAtTop = editor.contentHeight();

        editor.verticalScrollBar()->setValue(editor.verticalScrollBar()->maximum());
        QVERIFY(editor.verticalScrollBar()->value() > 0);
        int heightWhenScrolled = editor.contentHeight();

        QCOMPARE(heightWhenScrolled, heightAtTop);
    }

    // contentHeight() adds 2 * frameWidth() on top of the summed row heights to account for the
    // panel's border. Nothing else in this suite pins that term down: every other assertion
    // compares contentHeight() against itself or against maximumHeight(), which stays
    // self-consistent whether the frame term is there or not. This is a plain change detector —
    // ordinarily not something worth a test — but it is the only thing standing between a
    // regression that clips the last row and raises a scrollbar, and nobody noticing.
    void test_contentHeightIncludesTheFrameOnTopOfRowHeights()  // NOLINT
    {
        PropertyEditor editor;
        buildUpSubject(editor);
        editor.expandAll();

        // Pin the frame width down explicitly: with QFrame::Box it comes straight from
        // lineWidth + midLineWidth rather than the desktop style's PM_DefaultFrameWidth, so the
        // assertion below does not depend on whatever style happens to be active.
        editor.setFrameShape(QFrame::Box);
        editor.setLineWidth(3);
        editor.setMidLineWidth(0);
        QVERIFY(editor.frameWidth() > 0);

        QCOMPARE(editor.contentHeight(), sumOfVisibleRowHeights(editor) + 2 * editor.frameWidth());
    }

    // setEditorMode hides rows whenever "Show hidden" is off, which is the normal state of a
    // real property view, so a hidden row's height must not count towards the panel's size.
    void test_contentHeightExcludesHiddenRows()  // NOLINT
    {
        PropertyEditor editor;
        buildUpSubject(editor);
        editor.expandAll();

        const QModelIndex group = editor.model()->index(0, 0, QModelIndex());
        const QModelIndex hiddenRow = editor.model()->index(0, 0, group);
        QVERIFY(hiddenRow.isValid());

        const int hiddenRowHeight = subtreeHeight(editor, hiddenRow);
        const int heightBeforeHiding = editor.contentHeight();

        editor.setRowHidden(0, group, true);

        QCOMPARE(editor.contentHeight(), heightBeforeHiding - hiddenRowHeight);
    }

    // The realistic sequence below (test_expandAllStaysAccurateWhenACompoundRowArrivesMidTree)
    // fully covers expandAll()'s explicit refresh, because the mid-layout partial-signal case it
    // exercises is what actually happens in practice. collapseAll() has no equivalent realistic
    // reproduction — collapsing never has to reconcile a partial layout — so its own explicit
    // refresh can only be proven by isolation: signals are blocked around the call so the
    // pre-existing per-row connects cannot be the reason the cap ends up right, leaving only the
    // explicit refresh in collapseAll() to account for it.
    void test_collapseAllKeepsTheCapInSyncInIsolation()  // NOLINT
    {
        QWidget root;
        auto* editor = new PropertyEditor(&root);
        buildUpSubject(*editor);
        editor->expandAll();

        Gui::FreeCADStyle style;
        style.updateTransparency(&root, true);
        int expandedCap = editor->maximumHeight();
        QCOMPARE(expandedCap, editor->contentHeight());

        editor->blockSignals(true);
        editor->collapseAll();
        editor->blockSignals(false);
        int collapsedCap = editor->maximumHeight();

        QVERIFY(collapsedCap < expandedCap);
        QCOMPARE(collapsedCap, editor->contentHeight());
    }

    // A realistic sequence rather than an isolated one: build, expand, then have a row with its
    // own children (a compound property, e.g. Placement) arrive mid-tree — as adding a dynamic
    // property would — and expand again. A freshly inserted row starts out not recorded as
    // expanded, so this second expandAll() has to expand it for the first time while the rest
    // of the tree is already laid out. QTreeViewPrivate::layout() can emit expanded() for that
    // row while still mid-build, before the rows after it are accounted for, so a cap update
    // driven purely by that signal undercounts. Only the explicit refresh after
    // QTreeView::expandAll() returns sees the finished layout.
    void test_expandAllStaysAccurateWhenACompoundRowArrivesMidTree()  // NOLINT
    {
        QWidget root;
        auto* editor = new PropertyEditor(&root);
        buildUpSubjectExcludingProperty(*editor, "Placement");
        editor->expandAll();

        Gui::FreeCADStyle style;
        style.updateTransparency(&root, true);

        buildUpSubject(*editor);  // Placement, with its own Position/Angle/Axis children, arrives
        editor->expandAll();

        QCOMPARE(editor->maximumHeight(), editor->contentHeight());
    }

    // Over an opaque surface nothing is capped — the editor fills whatever it is given. The cap
    // must actually have engaged first, or observing QWIDGETSIZE_MAX proves nothing: an editor
    // that was never tagged transparent reports the same default without updateHeightLimit()
    // doing any work at all.
    void test_opaqueEditorIsUncapped()  // NOLINT
    {
        QWidget root;
        auto* editor = new PropertyEditor(&root);
        buildUpSubject(*editor);

        Gui::FreeCADStyle style;
        style.updateTransparency(&root, true);
        QVERIFY(editor->maximumHeight() < QWIDGETSIZE_MAX);

        style.updateTransparency(&root, false);

        QCOMPARE(editor->maximumHeight(), QWIDGETSIZE_MAX);
    }

    // Over a transparent surface the editor caps itself, so the panel stops at its rows.
    void test_transparentEditorCapsToContent()  // NOLINT
    {
        QWidget root;
        auto* editor = new PropertyEditor(&root);
        buildUpSubject(*editor);

        Gui::FreeCADStyle style;
        style.updateTransparency(&root, true);

        QVERIFY(editor->maximumHeight() < QWIDGETSIZE_MAX);
        QCOMPARE(editor->maximumHeight(), editor->contentHeight());
    }

    // Flipping the tag back must lift the cap, or a panel that leaves the overlay stays short.
    void test_capIsLiftedWhenTransparencyIsRevoked()  // NOLINT
    {
        QWidget root;
        auto* editor = new PropertyEditor(&root);
        buildUpSubject(*editor);

        Gui::FreeCADStyle style;
        style.updateTransparency(&root, true);
        // The cap must actually have engaged here, or lifting it below proves nothing.
        QVERIFY(editor->maximumHeight() < QWIDGETSIZE_MAX);

        style.updateTransparency(&root, false);

        QCOMPARE(editor->maximumHeight(), QWIDGETSIZE_MAX);
    }

    // Losing every row collapses the cap to nothing without needing a transparency change.
    void test_clearingContentCollapsesTheCap()  // NOLINT
    {
        QWidget root;
        auto* editor = new PropertyEditor(&root);
        buildUpSubject(*editor);

        Gui::FreeCADStyle style;
        style.updateTransparency(&root, true);
        QVERIFY(editor->maximumHeight() > 0);

        editor->buildUp({});

        QCOMPARE(editor->maximumHeight(), 0);
    }

    // A capped editor sits at the top of its tab page rather than floating in the middle.
    // QStackedLayout sets the page's geometry directly, so QWidget::setGeometry clamps the
    // height against the maximum while keeping the top-left corner. Introducing a page layout
    // here would route through QWidgetItem instead, which centres the excess.
    void test_cappedEditorAnchorsToTopOfTabPage()  // NOLINT
    {
        Gui::PropertyView propertyView;
        PropertyEditor* dataEditor = propertyView.propertyEditorData;
        buildUpSubject(*dataEditor);

        const int cappedHeight = dataEditor->contentHeight();
        QVERIFY(cappedHeight > 0);

        // Set the view to be transparent so the editor caps to its content height.
        Gui::FreeCADStyle style;
        style.updateTransparency(&propertyView, true);

        propertyView.resize(400, cappedHeight + 200);
        propertyView.show();
        QVERIFY(QTest::qWaitForWindowExposed(&propertyView));

        // The editor should be at the top of its tab page at y=0, not centered vertically.
        QCOMPARE(dataEditor->y(), 0);
        QCOMPARE(dataEditor->height(), cappedHeight);
    }

private:
    // Fills the editor from the test document object so the model has real rows.
    void buildUpSubject(PropertyEditor& editor)
    {
        std::map<std::string, App::Property*> properties;
        object->getPropertyMap(properties);

        Gui::PropertyEditor::PropertyModel::PropertyList list;
        for (const auto& [name, property] : properties) {
            list.emplace_back(name, std::vector<App::Property*> {property});
        }

        editor.buildUp(std::move(list));
    }

    // Same as buildUpSubject, but leaves out one named property, so a later buildUpSubject()
    // call makes that property arrive as a brand-new row.
    void buildUpSubjectExcludingProperty(PropertyEditor& editor, const char* excludedName)
    {
        std::map<std::string, App::Property*> properties;
        object->getPropertyMap(properties);

        Gui::PropertyEditor::PropertyModel::PropertyList list;
        for (const auto& [name, property] : properties) {
            if (name == excludedName) {
                continue;
            }
            list.emplace_back(name, std::vector<App::Property*> {property});
        }

        editor.buildUp(std::move(list));
    }

    // Independently recomputes the height contentHeight() is supposed to report for its visible,
    // expanded rows, without going through contentHeight() itself.
    int sumOfVisibleRowHeights(const PropertyEditor& editor, const QModelIndex& parent = QModelIndex())
    {
        int height = 0;
        for (int row = 0, count = editor.model()->rowCount(parent); row < count; ++row) {
            if (editor.isRowHidden(row, parent)) {
                continue;
            }

            const QModelIndex index = editor.model()->index(row, 0, parent);
            height += editor.visualRect(index).height();
            if (editor.isExpanded(index)) {
                height += sumOfVisibleRowHeights(editor, index);
            }
        }
        return height;
    }

    // Height a single row contributes while it is visible: its own row plus, if it is expanded,
    // everything under it. Measured before the row is hidden, so it captures what disappearing
    // that row is supposed to remove from contentHeight().
    int subtreeHeight(const PropertyEditor& editor, const QModelIndex& index)
    {
        int height = editor.visualRect(index).height();
        if (editor.isExpanded(index)) {
            height += sumOfVisibleRowHeights(editor, index);
        }
        return height;
    }

    App::Document* document = nullptr;
    App::DocumentObject* object = nullptr;
};

QTEST_MAIN(TestPropertyEditorSizing)

#include "PropertyEditorSizing.moc"
