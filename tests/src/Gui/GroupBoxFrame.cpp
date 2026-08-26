// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QGroupBox>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QScopeGuard>
#include <QStyleOptionGroupBox>
#include <QTest>

#include "src/App/InitApplication.h"

#include <Gui/Application.h>
#include <Gui/FreeCADStyle.h>
#include <Gui/StyleParameters/ParameterManager.h>
#include <Gui/StyleParameters/StyleContext.h>

// drawBoxBackground is protected on FreeCADStyle; a using-declaration republishes it so the
// mask can be exercised without going through a widget.
class ProbeStyle: public Gui::FreeCADStyle
{
public:
    using Gui::FreeCADStyle::drawBoxBackground;
};

// QGroupBox::initStyleOption is protected; a using-declaration republishes it so a test can
// build the very option Qt would hand the style.
class ProbeGroupBox: public QGroupBox
{
public:
    using QGroupBox::initStyleOption;
    using QGroupBox::QGroupBox;
};

class TestGroupBoxFrame: public QObject
{
    Q_OBJECT

public:
    TestGroupBoxFrame()
    {
        tests::initApplication();

        if (Gui::Application::Instance == nullptr) {
            new Gui::Application(true);
        }

        // Saturated, fully opaque colours so a single pixel probe is unambiguous, and a title
        // font far from any system default so a metric can only have come from the token.
        Gui::Application::Instance->styleParameterManager()->addSource(
            new Gui::StyleParameters::InMemoryParameterSource(
                {
                    {.name = "GroupBoxBorderColor", .value = "#ff0000"},
                    {.name = "GroupBoxBorderThickness", .value = "1px"},
                    {.name = "GroupBoxBorderRadius", .value = "0px"},
                    {.name = "GroupBoxBackground", .value = "#00ff00"},
                    {.name = "GroupBoxPadding", .value = "padding(12px)"},
                    {.name = "GroupBoxFlatBorderThickness", .value = "border_thickness(0px, top: 1px)"},
                    {.name = "GroupBoxFlatBorderRadius", .value = "0px"},
                    {.name = "GroupBoxFlatPadding", .value = "padding(0, top: 12px)"},
                    {.name = "GroupBoxTitlePadding", .value = "padding(horizontal: 6px, vertical: 0)"},
                    {.name = "GroupBoxTitleFontSize", .value = "10px"},
                    {.name = "GroupBoxTitleFontWeight", .value = "600"},
                    {.name = "GroupBoxTitleTextColor", .value = "#0000ff"},
                    // On an unrelated component so it cannot interfere with the GroupBox
                    // fixtures above: exercises the pt-unit branch of resolveFont.
                    {.name = "HeaderFontSize", .value = "12pt"},
                },
                {.name = "GroupBox Fixture"}
            )
        );

        // Registered last so it outranks the fixture above, and left empty so it costs nothing
        // until a test asks for a different value.
        overrides
            = new Gui::StyleParameters::InMemoryParameterSource({}, {.name = "GroupBox Overrides"});
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

    // A red border around a green fill: either one is unmistakable in a single pixel probe.
    static Gui::FreeCADStyle::BoxStyleDefinition borderedBox()
    {
        Gui::FreeCADStyle::BoxStyleDefinition box;
        box.background = QBrush(QColor(0, 255, 0));
        box.borderColor = Gui::FreeCADStyle::BorderColorsPerSide {
            .top = QColor(255, 0, 0),
            .right = QColor(255, 0, 0),
            .bottom = QColor(255, 0, 0),
            .left = QColor(255, 0, 0),
        };
        box.borderThickness = QMarginsF(1, 1, 1, 1);
        return box;
    }

    static QImage paintBoxWithMask(const QPainterPath& mask)
    {
        QImage canvas(40, 20, QImage::Format_ARGB32);
        canvas.fill(Qt::transparent);

        QPainter painter(&canvas);
        ProbeStyle::drawBoxBackground(&painter, QRect(0, 0, 40, 20), borderedBox(), mask);
        painter.end();

        return canvas;
    }

    // Renders a group box over an unmistakable parent colour, exactly as Qt would. @p extraState
    // covers the flags Qt only raises from real input, such as keyboard focus. @p parentColour
    // defaults to the fixture's title colour, so probes that need to tell the two apart pass
    // something else.
    static QImage paintGroupBox(
        ProbeGroupBox& box,
        QStyleOptionGroupBox& option,
        QStyle::State extraState = {},
        QColor parentColour = QColor(0, 0, 255)
    )
    {
        box.resize(200, 80);
        box.initStyleOption(&option);
        option.state |= extraState;

        QImage canvas(200, 80, QImage::Format_ARGB32);
        canvas.fill(parentColour);

        Gui::FreeCADStyle style;
        QPainter painter(&canvas);
        static_cast<QStyle*>(&style)->drawComplexControl(QStyle::CC_GroupBox, &option, &painter, &box);
        painter.end();

        return canvas;
    }

    // First and last row carrying title glyphs, on a canvas painted over white. The fixture's
    // red stroke, green fill and white parent all fail this test and the blue title passes it,
    // so what comes back is the glyphs' own vertical extent rather than the label rect's.
    static std::pair<int, int> titleInkRows(const QImage& canvas)
    {
        int firstRow = -1;
        int lastRow = -1;

        for (int row = 0; row < canvas.height(); ++row) {
            for (int column = 0; column < canvas.width(); ++column) {
                const QColor pixel = canvas.pixelColor(column, row);
                if (pixel.blue() > 60 && pixel.red() < 200 && pixel.green() < 200) {
                    firstRow = firstRow < 0 ? row : firstRow;
                    lastRow = row;
                    break;
                }
            }
        }

        return {firstRow, lastRow};
    }

    // The sub-control rects the style itself reports, so probe positions are never guessed.
    static QRect groupBoxRect(
        const QStyleOptionGroupBox& option,
        const QWidget* widget,
        QStyle::SubControl subControl
    )
    {
        Gui::FreeCADStyle style;
        return static_cast<QStyle*>(&style)
            ->subControlRect(QStyle::CC_GroupBox, &option, subControl, widget);
    }

private Q_SLOTS:

    // The whole point of the mask: the stroke is absent inside it, not merely covered up.
    void test_borderMaskCutsTheRingAndSparesTheFill()  // NOLINT
    {
        QPainterPath mask;
        mask.addRect(QRectF(0, 0, 40, 20));

        QPainterPath notch;
        notch.addRect(QRectF(15, 0, 10, 20));

        const QImage canvas = paintBoxWithMask(mask.subtracted(notch));

        QCOMPARE(canvas.pixelColor(5, 0).red(), 255);
        QCOMPARE(canvas.pixelColor(20, 0).red(), 0);
        QCOMPARE(canvas.pixelColor(35, 0).red(), 255);

        // The fill is never confined by the mask, so it runs unbroken under the notch.
        QCOMPARE(canvas.pixelColor(20, 10), QColor(0, 255, 0));
    }

    // Every pre-existing call site relies on the default, so an empty path must mean
    // "unrestricted" rather than "mask everything away".
    void test_anEmptyMaskLeavesTheBorderWhole()  // NOLINT
    {
        const QImage canvas = paintBoxWithMask({});

        QCOMPARE(canvas.pixelColor(5, 0).red(), 255);
        QCOMPARE(canvas.pixelColor(20, 0).red(), 255);
        QCOMPARE(canvas.pixelColor(35, 0).red(), 255);
    }

    // The Flat variant is token data, not painting code: it has to reach a token name that
    // carries the variant fragment, or flat group boxes silently keep the full frame.
    void test_flatVariantKeepsOnlyTheTopBorder()  // NOLINT
    {
        Gui::StyleParameters::StyleContext context;
        context.component = Gui::StyleParameters::StyleComponent::GroupBox;
        context.variant.set(
            Gui::StyleParameters::VariantSlot::FrameType,
            Gui::StyleParameters::FrameType::Flat
        );

        Gui::FreeCADStyle style;
        const Gui::FreeCADStyle::BoxStyleDefinition box = style.resolveBoxStyle(context);

        QVERIFY(box.borderThickness.has_value());
        QCOMPARE(box.borderThickness->top(), 1.0);
        QCOMPARE(box.borderThickness->right(), 0.0);
        QCOMPARE(box.borderThickness->bottom(), 0.0);
        QCOMPARE(box.borderThickness->left(), 0.0);
    }

    // GroupBoxTitlePadding only resolves if the Title element contributes "Title" to the token
    // name. Without the element registered it falls back to the root box padding.
    void test_titleElementResolvesItsOwnPadding()  // NOLINT
    {
        Gui::StyleParameters::StyleContext context;
        context.component = Gui::StyleParameters::StyleComponent::GroupBox;
        context.element = Gui::StyleParameters::StyleComponentElement::Title;

        Gui::FreeCADStyle style;
        const Gui::FreeCADStyle::BoxGeometryDefinition geometry = style.resolveBoxGeometry(context);

        QCOMPARE(geometry.padding.left(), 6.0);
        QCOMPARE(geometry.padding.top(), 0.0);
    }

    // A theme sets the title's size and weight; the rest of the font comes from the caller.
    void test_resolveFontAppliesSizeAndWeightTokens()  // NOLINT
    {
        Gui::StyleParameters::StyleContext context;
        context.component = Gui::StyleParameters::StyleComponent::GroupBox;
        context.element = Gui::StyleParameters::StyleComponentElement::Title;

        QFont base;
        base.setPixelSize(30);
        base.setFamily(QStringLiteral("Some Deliberate Family"));

        Gui::FreeCADStyle style;
        const QFont resolved = style.resolveFont(context, base);

        QCOMPARE(resolved.pixelSize(), 10);
        QCOMPARE(resolved.weight(), QFont::Weight(600));
        QCOMPARE(resolved.family(), QStringLiteral("Some Deliberate Family"));
    }

    // Every caller hands over the widget's own font, so a context with no font tokens has to
    // give it straight back rather than substituting a default.
    void test_resolveFontLeavesTheBaseAloneWithoutTokens()  // NOLINT
    {
        Gui::StyleParameters::StyleContext context;
        context.component = Gui::StyleParameters::StyleComponent::GroupBox;

        QFont base;
        base.setPixelSize(30);
        base.setWeight(QFont::Weight(400));

        Gui::FreeCADStyle style;
        const QFont resolved = style.resolveFont(context, base);

        QCOMPARE(resolved, base);
    }

    // A pt-unit token has to reach setPointSizeF, not the px-only setPixelSize path.
    void test_resolveFontAppliesPointSizeToken()  // NOLINT
    {
        Gui::StyleParameters::StyleContext context;
        context.component = Gui::StyleParameters::StyleComponent::Header;

        QFont base;
        base.setPixelSize(30);

        Gui::FreeCADStyle style;
        const QFont resolved = style.resolveFont(context, base);

        QCOMPARE(resolved.pointSizeF(), 12.0);
    }

    // The label rect has to be measured with the font the title is painted in. Qt measures it
    // from option->fontMetrics, which is the widget's font unless the style substitutes.
    void test_labelRectFollowsTheTokenFontNotTheWidgetFont()  // NOLINT
    {
        Gui::FreeCADStyle style;

        ProbeGroupBox box(QStringLiteral("Title"));
        box.resize(200, 80);
        QFont oversized = box.font();
        oversized.setPixelSize(30);
        box.setFont(oversized);

        QStyleOptionGroupBox option;
        box.initStyleOption(&option);

        const QRect label = static_cast<QStyle*>(&style)->subControlRect(
            QStyle::CC_GroupBox,
            &option,
            QStyle::SC_GroupBoxLabel,
            &box
        );

        QFont titleFont = box.font();
        titleFont.setPixelSize(10);
        titleFont.setWeight(QFont::Weight(600));

        // Two bounds rather than an exact height: QFusionStyle sizes the label from the text's
        // bounding box plus a couple of pixels of its own, and pinning that arithmetic here would
        // test Fusion's padding instead of the substitution this task exists for. Below the widget
        // font's height proves the widget font was not used; at or above the token font's bounding
        // box proves the label is sized for the font the title is painted in.
        QVERIFY(label.height() < QFontMetrics(oversized).height());
        QVERIFY(label.height() >= QFontMetrics(titleFont).boundingRect(box.title()).height());
    }

    // QGroupBox turns SC_GroupBoxContents into its own contents margins, so this is what every
    // layout inside a group box is spaced by.
    void test_untitledContentsMarginsAreExactlyTheTokenPadding()  // NOLINT
    {
        Gui::FreeCADStyle style;

        ProbeGroupBox box;
        box.setStyle(&style);
        box.resize(200, 80);

        QCOMPARE(box.contentsMargins(), QMargins(12, 12, 12, 12));
    }

    // The padding is the gap between the frame and its contents, and it is the same on all four
    // sides whether the box carries a title or not. The title's lower half hangs into the top
    // padding rather than being cleared on top of it.
    void test_aTitleLeavesTheFramePaddingUniform()  // NOLINT
    {
        Gui::FreeCADStyle style;

        ProbeGroupBox box(QStringLiteral("Title"));
        box.setStyle(&style);
        box.resize(200, 80);

        QStyleOptionGroupBox option;
        box.initStyleOption(&option);

        const QRect frame = groupBoxRect(option, &box, QStyle::SC_GroupBoxFrame);
        const QRect contents = groupBoxRect(option, &box, QStyle::SC_GroupBoxContents);

        QCOMPARE(contents, frame.adjusted(12, 12, -12, -12));

        // The widget's own margins carry the frame's top offset as well, because the frame starts
        // half a title below the widget. Everything beyond that offset is the token padding.
        const QMargins margins = box.contentsMargins();

        QCOMPARE(margins.left(), 12);
        QCOMPARE(margins.right(), 12);
        QCOMPARE(margins.bottom(), 12);
        QCOMPARE(margins.top(), frame.top() + 12);
    }

    // The title's descenders reach into the top padding, so the padding has to be deep enough to
    // stay clear of them; too shallow and the first row of contents collides with the title.
    void test_theTopPaddingClearsTheTitlesDescenders()  // NOLINT
    {
        Gui::FreeCADStyle style;

        // A descender on purpose: "Title" has none, and the clearance only matters for one.
        ProbeGroupBox box(QStringLiteral("Paging"));
        box.setStyle(&style);
        box.resize(200, 80);

        QStyleOptionGroupBox option;
        box.initStyleOption(&option);

        const QRect contents = groupBoxRect(option, &box, QStyle::SC_GroupBoxContents);
        const QRect label = groupBoxRect(option, &box, QStyle::SC_GroupBoxLabel);

        QVERIFY(label.bottom() < contents.top());
    }

    // Flat is token data all the way down. A flat box has no side or bottom line to stand off
    // from, so GroupBoxFlatPadding drops those three and keeps only the gap below the top line.
    void test_aFlatBoxPadsOnlyBelowItsTopLine()  // NOLINT
    {
        Gui::FreeCADStyle style;

        ProbeGroupBox box(QStringLiteral("Title"));
        box.setFlat(true);
        box.setStyle(&style);
        box.resize(200, 80);

        QStyleOptionGroupBox option;
        box.initStyleOption(&option);

        const QRect frame = groupBoxRect(option, &box, QStyle::SC_GroupBoxFrame);
        const QRect contents = groupBoxRect(option, &box, QStyle::SC_GroupBoxContents);

        QCOMPARE(contents, frame.adjusted(0, 12, 0, 0));

        const QMargins margins = box.contentsMargins();

        QCOMPARE(margins.left(), 0);
        QCOMPARE(margins.right(), 0);
        QCOMPARE(margins.bottom(), 0);
        QCOMPARE(margins.top(), frame.top() + 12);
    }

    // The masking tests below probe at rects the style itself reports, so they hold whether the
    // notch lands on the title or on empty space. This is the geometry that makes them mean
    // something: the frame's top edge has to run through the title band.
    void test_theTitleStraddlesTheFrameTopEdge()  // NOLINT
    {
        ProbeGroupBox box(QStringLiteral("Title"));
        box.resize(200, 80);
        QStyleOptionGroupBox option;
        box.initStyleOption(&option);

        const QRect frame = groupBoxRect(option, &box, QStyle::SC_GroupBoxFrame);
        const QRect label = groupBoxRect(option, &box, QStyle::SC_GroupBoxLabel);

        // Strictly inside the band on both counts. Merely touching is what an AlignTop vertical
        // alignment produces: the title ends on the frame's first row, so the notch takes the
        // stroke out from under empty space and leaves the top-left corner a detached stub.
        QVERIFY(label.top() < frame.top());
        QVERIFY(frame.top() < label.bottom());

        // Half the title hangs below the edge when it is centred on it; a third allows for
        // rounding without admitting the single-row overlap AlignTop leaves behind.
        QVERIFY(label.bottom() + 1 - frame.top() >= label.height() / 3);
    }

    // Straddling is measured above against the frame's own top edge, which the re-centring in
    // groupBoxSubControlRect() pins the label to whatever that edge turns out to be — so it
    // cannot see where the edge itself ended up. Fusion's AlignTop answer moves it below the
    // whole title band, costing the box a title's worth of content height and leaving the
    // title floating in the gap above a frame it no longer cuts into.
    void test_theFrameReclaimsTheTitleBandsUpperHalf()  // NOLINT
    {
        ProbeGroupBox box(QStringLiteral("Title"));
        box.resize(200, 80);
        QStyleOptionGroupBox option;
        box.initStyleOption(&option);

        const QRect frame = groupBoxRect(option, &box, QStyle::SC_GroupBoxFrame);
        const QRect label = groupBoxRect(option, &box, QStyle::SC_GroupBoxLabel);

        // From the widget's own top edge, the one reference the re-centring cannot move.
        const int reserved = frame.top() - option.rect.top();

        QVERIFY2(
            reserved <= (label.height() / 2) + 1,
            qPrintable(QStringLiteral("%1px reserved above the frame for a %2px title band")
                           .arg(reserved)
                           .arg(label.height()))
        );
    }

    // Straddling is not enough on its own — the glyphs have to sit *centred* on the border row.
    // Placing the label band by its full ascent-plus-descent line box, as the base style hands
    // it over, leaves a title without descenders reading a pixel and a half low.
    void test_theTitleInkIsCentredOnTheBorderRow()  // NOLINT
    {
        ProbeGroupBox box(QStringLiteral("Title"));
        QStyleOptionGroupBox option;

        // White, so the blue title ink is the only thing in the scan that is neither the red
        // stroke nor the green fill.
        const QImage canvas = paintGroupBox(box, option, {}, QColor(255, 255, 255));

        const QRect frame = groupBoxRect(option, &box, QStyle::SC_GroupBoxFrame);
        const auto [inkTop, inkBottom] = titleInkRows(canvas);

        QVERIFY(inkTop >= 0);
        QVERIFY(inkTop < frame.top());
        QVERIFY(inkBottom > frame.top());

        // Within a pixel, which is as close as an integer band centred on an integer row can be
        // pinned. The defect being guarded against is a pixel and a half, comfortably outside.
        QVERIFY(qAbs(((inkTop + inkBottom) / 2.0) - frame.top()) <= 1.0);
    }

    // The mask is built from the same label rect the title is painted in, so re-centring the
    // title has to carry the notch with it rather than leaving a gap beside the text.
    void test_theNotchFollowsTheTitleInk()  // NOLINT
    {
        ProbeGroupBox box(QStringLiteral("Title"));
        QStyleOptionGroupBox option;
        const QImage canvas = paintGroupBox(box, option, {}, QColor(255, 255, 255));

        const QRect frame = groupBoxRect(option, &box, QStyle::SC_GroupBoxFrame);
        const auto [inkTop, inkBottom] = titleInkRows(canvas);

        QVERIFY(inkTop >= 0);

        // Every row the glyphs occupy is inside the label rect, which is the rect the notch is
        // cut from, so moving one moves the other.
        const QRect label = groupBoxRect(option, &box, QStyle::SC_GroupBoxLabel);

        QVERIFY(inkTop >= label.top());
        QVERIFY(inkBottom <= label.bottom());

        // And no stroke survives anywhere across that span: whatever border colour is left on the
        // top row lies outside the label, never behind the text.
        for (int column = frame.left(); column <= frame.right(); ++column) {
            if (canvas.pixelColor(column, frame.top()) == QColor(255, 0, 0)) {
                QVERIFY(column < label.left() || column > label.right());
            }
        }
    }

    // The defect this whole change exists for: the stroke has to be absent under the title,
    // not covered by an opaque patch that only matches one background.
    void test_theBorderIsAbsentUnderTheTitle()  // NOLINT
    {
        ProbeGroupBox box(QStringLiteral("Title"));
        QStyleOptionGroupBox option;
        const QImage canvas = paintGroupBox(box, option);

        const QRect frame = groupBoxRect(option, &box, QStyle::SC_GroupBoxFrame);
        const QRect label = groupBoxRect(option, &box, QStyle::SC_GroupBoxLabel);

        // Three pixels left of the glyphs: inside the notch the 6px title padding opens, and
        // clear of the text itself.
        const int inNotch = label.left() - 3;
        QVERIFY(inNotch > frame.left() + 1);

        QCOMPARE(canvas.pixelColor(frame.left() + 1, frame.top()), QColor(255, 0, 0));

        // No red: the stroke is genuinely gone here, not covered over. Not an exact parent colour —
        // the fill sits half a pixel under where the border was and feathers into this row, which
        // is the fill staying whole under the title exactly as intended.
        QCOMPARE(canvas.pixelColor(inNotch, frame.top()).red(), 0);

        QCOMPARE(canvas.pixelColor(frame.right() - 2, frame.top()), QColor(255, 0, 0));
    }

    // The mask governs the border ring alone, so the box keeps its own surface under the title.
    void test_theFillRunsUnbrokenUnderTheTitle()  // NOLINT
    {
        ProbeGroupBox box(QStringLiteral("Title"));
        QStyleOptionGroupBox option;
        const QImage canvas = paintGroupBox(box, option);

        const QRect frame = groupBoxRect(option, &box, QStyle::SC_GroupBoxFrame);
        const QRect label = groupBoxRect(option, &box, QStyle::SC_GroupBoxLabel);

        QCOMPARE(canvas.pixelColor(label.left() - 3, frame.top() + 3), QColor(0, 255, 0));
    }

    // A checkable box's indicator sits on the frame edge too, so the notch has to cover it.
    void test_theNotchCoversTheCheckIndicator()  // NOLINT
    {
        ProbeGroupBox box(QStringLiteral("Title"));
        box.setCheckable(true);
        QStyleOptionGroupBox option;
        const QImage canvas = paintGroupBox(box, option);

        const QRect frame = groupBoxRect(option, &box, QStyle::SC_GroupBoxFrame);
        const QRect indicator = groupBoxRect(option, &box, QStyle::SC_GroupBoxCheckBox);

        QVERIFY(indicator.left() - 3 > frame.left() + 1);

        // No red: the stroke is genuinely gone here, not covered over. See
        // test_theBorderIsAbsentUnderTheTitle for why this isn't an exact parent-colour check.
        QCOMPARE(canvas.pixelColor(indicator.left() - 3, frame.top()).red(), 0);
    }

    // setCheckable gives a group box strong focus, so Tab can land on one. The label is the only
    // part of it that can say so, and nothing else in the box changes appearance with focus.
    void test_aKeyboardFocusedBoxMarksItsLabel()  // NOLINT
    {
        ProbeGroupBox box(QStringLiteral("Title"));
        box.setCheckable(true);

        QStyleOptionGroupBox restingOption;
        const QImage resting = paintGroupBox(box, restingOption);

        QStyleOptionGroupBox focusedOption;
        const QImage focused = paintGroupBox(
            box,
            focusedOption,
            QStyle::State_HasFocus | QStyle::State_KeyboardFocusChange
        );

        // The base style outlines the label rect, so its own edges are where the difference is
        // certain to land whatever the title's glyphs happen to cover.
        const QRect label = groupBoxRect(focusedOption, &box, QStyle::SC_GroupBoxLabel);
        const int probeRow = label.center().y();

        QVERIFY(
            focused.pixelColor(label.left(), probeRow) != resting.pixelColor(label.left(), probeRow)
        );
        QVERIFY(
            focused.pixelColor(label.right() - 1, probeRow)
            != resting.pixelColor(label.right() - 1, probeRow)
        );
    }

    // Nothing to mask, so nothing may be cut.
    void test_anUntitledBoxKeepsAnUnbrokenBorder()  // NOLINT
    {
        ProbeGroupBox box;
        QStyleOptionGroupBox option;
        const QImage canvas = paintGroupBox(box, option);

        const QRect frame = groupBoxRect(option, &box, QStyle::SC_GroupBoxFrame);

        for (int x = frame.left() + 1; x < frame.right() - 1; ++x) {
            QCOMPARE(canvas.pixelColor(x, frame.top()), QColor(255, 0, 0));
        }
    }

    // Flat is token data: the top line survives and the other three sides do not.
    void test_aFlatBoxDrawsOnlyItsTopEdge()  // NOLINT
    {
        ProbeGroupBox box(QStringLiteral("Title"));
        box.setFlat(true);
        QStyleOptionGroupBox option;
        const QImage canvas = paintGroupBox(box, option);

        const QRect frame = groupBoxRect(option, &box, QStyle::SC_GroupBoxFrame);

        QCOMPARE(canvas.pixelColor(frame.right() - 2, frame.top()), QColor(255, 0, 0));
        QVERIFY(canvas.pixelColor(frame.left(), frame.center().y()) != QColor(255, 0, 0));
        QVERIFY(canvas.pixelColor(frame.center().x(), frame.bottom()) != QColor(255, 0, 0));
    }

    // The leading-edge inset the notch relies on only applies to a leading-aligned title;
    // a centred one has to stay centred on the frame, not drift by the padding as well.
    void test_aCentredTitleStaysCentredOnTheFrame()  // NOLINT
    {
        ProbeGroupBox box(QStringLiteral("Title"));
        box.setAlignment(Qt::AlignHCenter);
        box.resize(200, 80);
        QStyleOptionGroupBox option;
        box.initStyleOption(&option);

        const QRect frame = groupBoxRect(option, &box, QStyle::SC_GroupBoxFrame);
        const QRect label = groupBoxRect(option, &box, QStyle::SC_GroupBoxLabel);

        QVERIFY(qAbs(label.center().x() - frame.center().x()) <= 1);
    }

    // A leading-aligned title starts exactly where the contents start. Fusion lays the label out
    // from the option rect's width alone, so this only holds because of the padding shift.
    void test_aLeftToRightTitleAlignsWithTheContentsEdge()  // NOLINT
    {
        ProbeGroupBox box(QStringLiteral("Title"));
        box.setLayoutDirection(Qt::LeftToRight);
        box.resize(200, 80);
        QStyleOptionGroupBox option;
        box.initStyleOption(&option);

        const QRect contents = groupBoxRect(option, &box, QStyle::SC_GroupBoxContents);
        const QRect label = groupBoxRect(option, &box, QStyle::SC_GroupBoxLabel);

        QCOMPARE(label.left(), contents.left());
    }

    // The same contract mirrored. The shift changes sign here and nothing else does, so this is
    // the only thing standing between that sign and a silently misplaced right-to-left title.
    void test_aRightToLeftTitleAlignsWithTheContentsEdge()  // NOLINT
    {
        ProbeGroupBox box(QStringLiteral("Title"));
        box.setLayoutDirection(Qt::RightToLeft);
        box.resize(200, 80);
        QStyleOptionGroupBox option;
        box.initStyleOption(&option);

        const QRect contents = groupBoxRect(option, &box, QStyle::SC_GroupBoxContents);
        const QRect label = groupBoxRect(option, &box, QStyle::SC_GroupBoxLabel);

        QCOMPARE(label.right(), contents.right());
    }

    // The shipping theme leaves the group box transparent, and that is the configuration the
    // original defect lived in: an opaque patch over the border only ever matches one surface.
    // With no fill of its own the notch has to show the parent through exactly.
    void test_aTransparentBoxShowsTheParentThroughTheNotch()  // NOLINT
    {
        const auto restore = overrideToken("GroupBoxBackground", "opacity(#ffffff, 0%)");

        ProbeGroupBox box(QStringLiteral("Title"));
        QStyleOptionGroupBox option;
        const QImage canvas = paintGroupBox(box, option);

        const QRect frame = groupBoxRect(option, &box, QStyle::SC_GroupBoxFrame);
        const QRect label = groupBoxRect(option, &box, QStyle::SC_GroupBoxLabel);

        QCOMPARE(canvas.pixelColor(frame.left() + 1, frame.top()), QColor(255, 0, 0));

        // Exactly the parent colour, not merely free of red: with no fill there is nothing to
        // feather into the row, so anything else here is a patch the mask failed to remove.
        QCOMPARE(canvas.pixelColor(label.left() - 3, frame.top()), QColor(0, 0, 255));
    }
};

QTEST_MAIN(TestGroupBoxFrame)

#include "GroupBoxFrame.moc"
