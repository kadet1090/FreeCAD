// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QApplication>
#include <QImage>
#include <QLineEdit>
#include <QTest>
#include <QTreeWidgetItem>

#include "src/App/InitApplication.h"

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>

#include <Gui/Application.h>
#include <Gui/FreeCADStyle.h>
#include <Gui/SoFCDB.h>
#include <Gui/StyleParameters/ParameterManager.h>
#include <Gui/Tree.h>

namespace
{

/// The item box's fill, opaque so its edges can be read back off the rendered viewport.
constexpr QRgb ItemColor = qRgb(255, 0, 0);

/// What the box keeps between its content and its own right edge, as stated in the fixture.
constexpr int ItemPaddingRight = 9;

/// What the field is allowed to spend on itself: the fixture's border on each side, plus the two
/// pixels QLineEdit insets its text by. Padding is not in it - the field stands in for a label.
constexpr int FieldChrome = 2 * 1 + 4;

/// The name the object under test carries, long enough that a field sized for it is
/// unmistakably narrower than the row it sits in.
const QString ObjectLabel = QStringLiteral("Body");

/// The right edge of the item box painted on the row through @p rowCentre, or -1 if the row
/// carries no box at all. Child widgets are left out: the editor is one, and it covers exactly
/// what is being asked about here.
int itemBoxRight(QWidget& viewport, int rowCentre)
{
    QImage canvas(viewport.size(), QImage::Format_ARGB32);
    canvas.fill(Qt::black);
    viewport.render(&canvas, QPoint(), QRegion(), QWidget::DrawWindowBackground);

    for (int x = canvas.width() - 1; x >= 0; --x) {
        if (canvas.pixel(x, rowCentre) == ItemColor) {
            return x;
        }
    }

    return -1;
}

}  // namespace

/// The tree lays a rename field out over the item it belongs to. Over a transparent surface that
/// item is a box hugging its own content, so the field has to be measured against the box rather
/// than against the whole row.
class TestTreeItemEditor: public QObject
{
    Q_OBJECT

public:
    TestTreeItemEditor()
    {
        tests::initApplication();

        if (Gui::Application::Instance == nullptr) {
            // View provider types have to be registered before a document can build one, and it
            // is that view provider which puts an object into the tree at all.
            Gui::Application::initTypes();
            new Gui::Application(true);
        }

        // Stated here rather than read from the shipped theme: these tests are about which edge
        // the field lands on, and pinning them to the theme's own numbers would make a future
        // retune of the tree look like a regression.
        Gui::Application::Instance->styleParameterManager()->addSource(
            new Gui::StyleParameters::InMemoryParameterSource(
                {
                    {.name = "DocumentTreeItemBackground", .value = "#ff0000"},
                    {.name = "DocumentTreeItemPadding",
                     .value = "padding(left: 5px, right: 9px, vertical: 2px)"},
                    // No margin, as the shipped themes state it: the delegate insets the box by
                    // one before handing it over and paintBox() insets it again, so a non-zero
                    // margin here would measure that double inset rather than this fix.
                    {.name = "DocumentTreeItemMargin", .value = "padding(0px)"},
                    // A rounded corner would put the box's own edge somewhere other than on the
                    // row's centre line, which is where it is read back.
                    {.name = "DocumentTreeItemBorderRadius", .value = "0px"},
                    {.name = "DocumentTreeItemSpacing", .value = "0px"},
                    {.name = "FormControlMinHeight", .value = "26px"},
                    {.name = "LineEditPadding", .value = "padding(horizontal: 4px, vertical: 2px)"},
                    {.name = "LineEditBorderThickness", .value = "1px"},
                },
                {.name = "Tree Item Editor Fixture"}
            )
        );

        QApplication::setStyle(new Gui::FreeCADStyle());
    }

private Q_SLOTS:
    void initTestCase()
    {
        // A Gui::Document builds view providers, and those build Coin nodes.
        if (!Gui::SoFCDB::isInitialized()) {
            Gui::SoFCDB::init();
        }
    }

    void init()
    {
        tree = new Gui::TreeWidget("TestTree");
        tree->resize(400, 300);
        tree->show();

        // The tree hears about a document and about its objects through separate signals, and it
        // only starts listening for the second once it has built the document's own item.
        document = App::GetApplication().newDocument("tree", "tester", {.createView = false});
        QTRY_COMPARE(tree->topLevelItemCount(), 1);

        auto* object = document->addObject("App::DocumentObjectGroup", "Body");
        object->Label.setValue(ObjectLabel.toStdString());
        QTRY_COMPARE(tree->topLevelItem(0)->childCount(), 1);

        tree->topLevelItem(0)->setExpanded(true);
        QTRY_VERIFY(!tree->visualItemRect(objectItem()).isEmpty());
    }

    void cleanup()
    {
        delete tree;
        tree = nullptr;
        App::GetApplication().closeDocument("tree");
    }

    // The field used to be laid out over the whole row while the box it sits in hugged the
    // label, so a rename ran out of its own item and off towards the panel's edge.
    void test_theBoxClosesAroundTheFieldOverATransparentSurface()  // NOLINT
    {
        makeTransparent();
        QLineEdit* editor = openEditor();
        QVERIFY(editor);

        const QRect item = tree->visualItemRect(objectItem());
        QCOMPARE(
            itemBoxRight(*tree->viewport(), item.center().y()),
            editor->geometry().right() + ItemPaddingRight
        );
    }

    // Both halves of hugging: wide enough to show the name it is editing, and no wider than that
    // name needs. Qt lays an item's editor out before it fills it in, so a field sized once and
    // never resized comes out at its empty width.
    void test_theFieldIsSizedForTheNameItEdits()  // NOLINT
    {
        makeTransparent();
        QLineEdit* editor = openEditor();
        QVERIFY(editor);

        const int label = editor->fontMetrics().horizontalAdvance(ObjectLabel);
        QVERIFY(editor->width() > label);
        QVERIFY(editor->width() < label * 3);
    }

    void test_theBoxFollowsTheFieldAsTheNameGrows()  // NOLINT
    {
        makeTransparent();
        QLineEdit* editor = openEditor();
        QVERIFY(editor);
        const int before = editor->width();

        QTest::keyClicks(editor, QStringLiteral("A much longer name"));

        QVERIFY(editor->width() > before);
        const QRect item = tree->visualItemRect(objectItem());
        QCOMPARE(
            itemBoxRight(*tree->viewport(), item.center().y()),
            editor->geometry().right() + ItemPaddingRight
        );
    }

    // The box's padding is worth less than a character of the name: a field that does not fit
    // takes the padding for itself, and the box closes tight on it.
    void test_theFieldTakesTheBoxPaddingWhenTheNameDoesNotFit()  // NOLINT
    {
        makeTransparent();
        QLineEdit* editor = openEditor();
        QVERIFY(editor);

        QTest::keyClicks(
            editor,
            QStringLiteral("a name far, far too long for a tree this narrow to ever show in full")
        );

        const QRect item = tree->visualItemRect(objectItem());
        QCOMPARE(editor->geometry().right(), item.right());
        QCOMPARE(itemBoxRight(*tree->viewport(), item.center().y()), editor->geometry().right());
    }

    // A form control's padding on top of the item's own would push the name right and cut its
    // tail off, which is what an in-place rename must not do.
    void test_theFieldSpendsNothingOnPaddingOfItsOwn()  // NOLINT
    {
        makeTransparent();
        QLineEdit* editor = openEditor();
        QVERIFY(editor);

        editor->clear();

        QCOMPARE(editor->width(), FieldChrome);
    }

    // Docked, the item is the full-width row every other item view draws, and the field keeps the
    // whole of it the way Qt lays every item editor out.
    void test_theFieldKeepsTheWholeRowWhenDocked()  // NOLINT
    {
        QLineEdit* editor = openEditor();
        QVERIFY(editor);

        const QRect row = tree->visualItemRect(objectItem());
        QVERIFY(editor->geometry().right() >= row.right() - ItemPaddingRight);
    }

    // The view places the field inside the row it edits, so the field cannot stand on the floor
    // a form control keeps against a layout: it would hang over the rows around it. The fixture
    // states that floor taller than a row for exactly this reason.
    void test_theFieldIsNoTallerThanTheRowItEdits()  // NOLINT
    {
        QLineEdit* editor = openEditor();
        QVERIFY(editor);

        const int row = tree->visualItemRect(objectItem()).height();

        QVERIFY2(
            editor->height() <= row,
            qPrintable(QStringLiteral("field %1px in a %2px row").arg(editor->height()).arg(row))
        );
    }

private:
    void makeTransparent()
    {
        auto* style = static_cast<Gui::FreeCADStyle*>(QApplication::style());
        style->updateTransparency(tree, true);
        QVERIFY(Gui::FreeCADStyle::isTransparent(tree));
    }

    QTreeWidgetItem* objectItem() const
    {
        return tree->topLevelItem(0)->child(0);
    }

    QLineEdit* openEditor()
    {
        tree->editItem(objectItem(), 0);
        return tree->viewport()->findChild<QLineEdit*>();
    }

    Gui::TreeWidget* tree = nullptr;
    App::Document* document = nullptr;
};

QTEST_MAIN(TestTreeItemEditor)
#include "TreeItemEditor.moc"
