// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QApplication>
#include <QIcon>
#include <QTest>
#include <QTreeWidgetItem>

#include "src/App/InitApplication.h"

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>

#include <Gui/Application.h>
#include <Gui/SoFCDB.h>
#include <Gui/Tree.h>

/// A tree row's icon has to hold one device pixel per device pixel the tree rasterised for it.
/// Everything here is about the icon's *resolution*, so it only means anything on a scaled
/// display — hence the ratio this suite forces on itself in main().
///
/// The composite these assertions read is built from the object icon and, when the preference is
/// on, a visibility marker beside it. Only its height is asserted: that holds either way, and the
/// preference comes from the user's own configuration.
class TestTreeIconResolution: public QObject
{
    Q_OBJECT

    /// The display ratio main() forces. Fractional on purpose: at an integer ratio a stretched
    /// icon is still stretched, but the arithmetic hides it.
    static constexpr qreal PixelRatio = 1.25;

    /// Logical height the tree is told to rasterise its object icons at.
    static constexpr int RasterisedExtent = 16;

public:
    TestTreeIconResolution()
    {
        tests::initApplication();

        if (Gui::Application::Instance == nullptr) {
            // It is the view provider that puts an object into the tree at all, and it is the
            // view provider's icon whose resolution this suite is about.
            Gui::Application::initTypes();
            new Gui::Application(true);  // NOLINT(cppcoreguidelines-owning-memory)
        }
    }

private Q_SLOTS:
    void initTestCase()  // NOLINT
    {
        // A Gui::Document builds view providers, and those build Coin nodes.
        if (!Gui::SoFCDB::isInitialized()) {
            Gui::SoFCDB::init();
        }
    }

    void init()  // NOLINT
    {
        tree = new Gui::TreeWidget("TestIconResolutionTree");
        tree->resize(400, 300);
        // Stated rather than inherited: left unset, the view falls back to a style metric, and
        // this suite is about the icon matching whatever height the tree asked for.
        tree->setIconSize(QSize(RasterisedExtent, RasterisedExtent));
        tree->show();

        document = App::GetApplication().newDocument("icons", "tester", {.createView = false});
        QTRY_COMPARE(tree->topLevelItemCount(), 1);

        document->addObject("App::DocumentObjectGroup", "Body");
        QTRY_COMPARE(tree->topLevelItem(0)->childCount(), 1);
        tree->topLevelItem(0)->setExpanded(true);
    }

    void cleanup()  // NOLINT
    {
        App::GetApplication().closeDocument(document->getName());
        document = nullptr;
        delete tree;
        tree = nullptr;
    }

    // The tree rasterises an object's icon for the display, then lays the row out around it. Read
    // that icon back as a device size — which is what QIcon reports — and the row reserves a cell
    // the display ratio too large, so the icon is stretched to fill it and goes soft. This is the
    // whole defect, and it is invisible to any assertion made at an unscaled ratio.
    void test_rowIconIsAsTallAsTheIconTheTreeRasterised()  // NOLINT
    {
        QCOMPARE(tree->devicePixelRatioF(), PixelRatio);

        const QIcon icon = objectItem()->icon(0);
        QVERIFY(!icon.isNull());

        QCOMPARE(
            Gui::logicalIconSize(icon, tree->devicePixelRatioF(), QIcon::Off).height(),
            RasterisedExtent
        );
    }

    // And the pixels are really there: asked for the size it occupies, the icon answers at the
    // display's ratio rather than handing back a ratio-1 raster for the row to stretch.
    void test_rowIconKeepsTheDisplayRatio()  // NOLINT
    {
        const QIcon icon = objectItem()->icon(0);
        const QSize logical = Gui::logicalIconSize(icon, PixelRatio, QIcon::Off);
        const QPixmap raster = icon.pixmap(logical, PixelRatio, QIcon::Normal, QIcon::Off);

        QCOMPARE(raster.devicePixelRatio(), PixelRatio);
        QCOMPARE(raster.deviceIndependentSize().toSize(), logical);
    }

private:
    QTreeWidgetItem* objectItem() const
    {
        return tree->topLevelItem(0)->child(0);
    }

    Gui::TreeWidget* tree = nullptr;
    App::Document* document = nullptr;
};

int main(int argc, char* argv[])
{
    // Qt reads this once, when the application is constructed, so it cannot be set from a test.
    qputenv("QT_SCALE_FACTOR", "1.25");

    QApplication app(argc, argv);  // NOLINT(misc-const-correctness)
    TestTreeIconResolution suite;
    return QTest::qExec(&suite, argc, argv);
}

#include "TreeIconResolution.moc"
