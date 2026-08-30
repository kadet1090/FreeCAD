// SPDX-License-Identifier: LGPL-2.1-or-later
/****************************************************************************
 *                                                                          *
 *   Copyright (c) 2026 Kacper Donat <kacper@kadet.net>                     *
 *                                                                          *
 *   This file is part of FreeCAD.                                          *
 *                                                                          *
 *   FreeCAD is free software: you can redistribute it and/or modify it     *
 *   under the terms of the GNU Lesser General Public License as            *
 *   published by the Free Software Foundation, either version 2.1 of the   *
 *   License, or (at your option) any later version.                        *
 *                                                                          *
 *   FreeCAD is distributed in the hope that it will be useful, but         *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of             *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU Lesser General Public       *
 *   License along with FreeCAD. If not, see                                *
 *   <https://www.gnu.org/licenses/>.                                       *
 *                                                                          *
 ***************************************************************************/

#include "GeometrySelectorWidget.h"

#include <algorithm>
#include <functional>
#include <ranges>

#include <App/Application.h>
#include <Base/Parameter.h>

#include <QCoreApplication>
#include <QEnterEvent>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QScrollArea>
#include <QStackedLayout>
#include <QStyleOption>
#include <QStyleOptionComboBox>
#include <QStyleOptionFrame>
#include <QStylePainter>
#include <QToolButton>
#include <QVBoxLayout>

#include <App/Document.h>
#include <App/DocumentObject.h>

#include "Application.h"
#include "Document.h"
#include "ElideLabel.h"
#include "FreeCADStyle.h"
#include "GeometrySelectorPopup.h"
#include "IconManager.h"
#include "ViewProvider.h"

using namespace Gui;

// ---------------------------------------------------------------------------
// Local constants and helpers.
// ---------------------------------------------------------------------------

namespace
{
/// Standard glyph size for the inline action icons.
constexpr int IconSize = 16;

/// The reference list grows to this many row strides before it caps and scrolls; the
/// fractional part leaves the next row partially visible to signal more content.
constexpr double MaxVisibleRows = 3.25;

/// At or below this many rows the list fits without scrolling and is laid out directly;
/// beyond it the rows go into a height-capped QScrollArea.
constexpr int MaxRowsWithoutScroll = 3;

/// Opacity of the scrim that dims the reference list beneath the selecting overlay; high
/// enough that the committed references stay only barely visible through it.
constexpr double ScrimOpacity = 0.94;

// Style-metric fallbacks used only when no Gui::Application (and thus no
// FreeCADStyle) is available, e.g. in the headless test harness. In the running
// application these are superseded by the resolved List item box geometry.
constexpr QMargins FallbackItemPadding {6, 4, 6, 4};
constexpr int FallbackSpacing = 6;

/// True multiset equality: same elements with the same multiplicity, order-independent.
/// Lists are tiny, so is_permutation's O(n²) is fine.
bool referencesEqualAsSet(
    const std::vector<GeometryReference>& left,
    const std::vector<GeometryReference>& right
)
{
    return std::ranges::is_permutation(left, right);
}

/// The number of picks a selector remembers unless a caller says otherwise.
int defaultHistoryLength()
{
    const ParameterGrp::handle group = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Selection"
    );
    return static_cast<int>(group->GetInt("GeometrySelectorHistoryLength", 5));
}

/// A compact, flat (auto-raise) icon button styled by the ambient QStyle.
QToolButton* makeActionButton(QWidget* parent, const QIcon& icon)
{
    auto* button = new QToolButton(parent);
    button->setProperty("controlSize", "internal");
    button->setAutoRaise(true);
    button->setIcon(icon);
    button->setIconSize(QSize(IconSize, IconSize));
    return button;
}

/// Renders @p button as a flat Link-variant tool button painted by FreeCADStyle — a
/// borderless control whose auto-raise state maps to the Link ButtonType variant.
void styleAsLinkButton(QToolButton* button)
{
    button->setAutoRaise(true);  // auto-raise resolves to the Link ButtonType variant
    if (Gui::Application::Instance) {
        button->setStyle(Gui::Application::Instance->freeCADStyle());
    }
}

/// A chromed (non-flat) text tool button at the Internal (18px) control size, painted by
/// FreeCADStyle. When @p primary it also carries the accent Primary ButtonType variant.
QToolButton* makeInternalTextButton(QWidget* parent, const QString& text, bool primary)
{
    auto* button = new QToolButton(parent);
    button->setText(text);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setProperty("controlSize", "internal");
    if (primary) {
        button->setProperty("buttonType", "primary");
    }
    if (Gui::Application::Instance) {
        button->setStyle(Gui::Application::Instance->freeCADStyle());
    }
    return button;
}

}  // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

GeometrySelectorWidget::GeometrySelectorWidget(GeometryQuantity mode, QWidget* parent)
    : QWidget(parent)
    , m_historyLength {defaultHistoryLength()}
    , m_selection(new GeometrySelection(mode, this))
    , m_contentLayout(nullptr)
{
    // Makes real keyboard focus show the focused style like any input.
    setFocusPolicy(Qt::StrongFocus);
    // Repaint on pointer enter/leave so the frame reflects its hovered background.
    setAttribute(Qt::WA_Hover);

    // The outer layout insets child widgets to the frame border + padding drawn
    // by paintEvent; the concrete margins come from applyStyleMetrics().
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setSpacing(0);

    auto* contentContainer = new QWidget(this);
    m_contentLayout = new QVBoxLayout(contentContainer);
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(contentContainer);

    // Keep the combo index in step with external reference changes (bound-property loads).
    connect(
        m_selection,
        &GeometrySelection::referencesChanged,
        this,
        &GeometrySelectorWidget::reconcileIndexFromReferences
    );
    // Captured on the way in and compared on the way out: a cancel is told apart directly via
    // GeometrySelection::wasCancelled(), but a session that finishes unchanged — nothing new was
    // ever picked — is recognised the same way a fresh custom pick's novelty is: by comparing
    // what came out to what went in.
    connect(m_selection, &GeometrySelection::selectionModeEntered, this, [this] {
        m_referencesAtSessionStart = m_selection->references();
    });
    connect(
        m_selection,
        &GeometrySelection::selectionModeExited,
        this,
        &GeometrySelectorWidget::captureHistoryEntry
    );
    // A remembered pick outlives the object it names. Nothing else prunes it, and building the
    // dropdown reads the object for its icon and label.
    m_objectDeletedConnection = App::GetApplication().signalDeletedObject.connect(
        [this](const App::DocumentObject& deleted) { forgetHistoryFor(&deleted); }
    );
    // React to model changes — every signal simply rebuilds the visible rows.
    connect(m_selection, &GeometrySelection::referencesChanged, this, &GeometrySelectorWidget::rebuildRows);
    connect(
        m_selection,
        &GeometrySelection::selectionModeEntered,
        this,
        &GeometrySelectorWidget::rebuildRows
    );
    connect(
        m_selection,
        &GeometrySelection::selectionModeExited,
        this,
        &GeometrySelectorWidget::rebuildRows
    );

    // Paint as a line-edit panel — signal to Qt that we draw our own background.
    setAutoFillBackground(false);

    applyStyleMetrics();
    rebuildRows();
}

GeometrySelectorWidget::GeometrySelectorWidget(QWidget* parent)
    : GeometrySelectorWidget(GeometryQuantity::Single, parent)
{}

GeometryQuantity GeometrySelectorWidget::quantity() const
{
    return m_selection->quantity();
}

void GeometrySelectorWidget::setQuantity(GeometryQuantity mode)
{
    if (m_selection->quantity() == mode) {
        return;
    }
    m_selection->setQuantity(mode);
    // The mode changes both the fixed-height rule and the rendered rows.
    applyStyleMetrics();
    rebuildRows();
}

// ---------------------------------------------------------------------------
// Combo mode — predefined options with QComboBox-like read-back.
// ---------------------------------------------------------------------------

void GeometrySelectorWidget::setOptions(std::vector<GeometrySelectorOption> options)
{
    m_options = std::move(options);
    m_currentIndex = -1;             // a changed option set invalidates the index
    reconcileIndexFromReferences();  // reverse-match against any references already present
    applyStyleMetrics();             // re-resolve layout metrics for the current mode
    rebuildRows();
}

void GeometrySelectorWidget::addOption(GeometrySelectorOption option)
{
    m_options.push_back(std::move(option));
    reconcileIndexFromReferences();
    applyStyleMetrics();
    rebuildRows();
}

void GeometrySelectorWidget::setAllowCustom(bool on)
{
    if (m_allowCustom == on) {
        return;
    }
    m_allowCustom = on;
    reconcileIndexFromReferences();
    // Turning Custom off can strand the index at the old Custom slot (n+h), which
    // reconcileIndexFromReferences() leaves alone when nothing matches and Custom is disabled.
    clampCurrentIndex();
    rebuildRows();
}

bool GeometrySelectorWidget::isComboMode() const
{
    return !m_options.empty();
}

int GeometrySelectorWidget::customIndex() const
{
    return m_allowCustom ? static_cast<int>(m_options.size() + m_history.size()) : -1;
}

bool GeometrySelectorWidget::isCustomIndex(int index) const
{
    return m_allowCustom && index == customIndex();
}

int GeometrySelectorWidget::currentIndex() const
{
    return m_currentIndex;
}

void GeometrySelectorWidget::setCurrentIndex(int index)
{
    // Ignore out-of-range indices (QComboBox-like). Valid: -1, any predefined option, any
    // remembered pick, and the Custom index only when Custom is enabled.
    if (index < -1 || index > lastValidIndex()) {
        return;
    }
    if (index == m_currentIndex) {
        return;
    }
    m_currentIndex = index;

    m_applyingChoice = true;
    if (isCustomIndex(index)) {
        // The Custom entry turns the widget back into a free-pick Select Box.
        m_selection->startSelecting();
    }
    else if (index >= 0 && index < static_cast<int>(m_options.size())) {
        m_selection->setReferences(m_options[index].references);
    }
    else if (const int offset = historyOffsetOf(index); offset >= 0) {
        // A remembered pick is a finished one: apply it, never restart the session it came from.
        m_selection->setReferences(m_history[offset].references);
    }
    m_applyingChoice = false;

    Q_EMIT currentIndexChanged(index);
    rebuildRows();
}

QVariant GeometrySelectorWidget::currentData() const
{
    if (m_currentIndex >= 0 && m_currentIndex < static_cast<int>(m_options.size())) {
        return m_options[m_currentIndex].userData;
    }
    if (isCustomIndex(m_currentIndex)) {
        return GeometrySelectorOption::customEntry().userData;
    }
    return {};
}

QString GeometrySelectorWidget::currentText() const
{
    if (m_currentIndex >= 0 && m_currentIndex < static_cast<int>(m_options.size())) {
        return m_options[m_currentIndex].label;
    }
    if (const int offset = historyOffsetOf(m_currentIndex); offset >= 0) {
        return m_history[offset].label;
    }
    if (isCustomIndex(m_currentIndex)) {
        return GeometrySelectorOption::customEntry().label;
    }
    return {};
}

const GeometrySelectorOption* GeometrySelectorWidget::currentOption() const
{
    if (m_currentIndex >= 0 && m_currentIndex < static_cast<int>(m_options.size())) {
        return &m_options[m_currentIndex];
    }
    return nullptr;
}

void GeometrySelectorWidget::reconcileIndexFromReferences()
{
    if (m_applyingChoice || !isComboMode()) {
        return;
    }
    const std::vector<GeometryReference>& references = m_selection->references();
    if (references.empty()) {
        return;  // an empty set cannot be told apart among logical options — leave as-is
    }

    int resolved = -1;
    for (std::size_t index = 0; index < m_options.size(); ++index) {
        if (referencesEqualAsSet(m_options[index].references, references)) {
            resolved = static_cast<int>(index);
            break;
        }
    }
    if (resolved < 0) {
        for (std::size_t entry = 0; entry < m_history.size(); ++entry) {
            if (referencesEqualAsSet(m_history[entry].references, references)) {
                resolved = static_cast<int>(m_options.size() + entry);
                break;
            }
        }
    }
    if (resolved < 0) {
        resolved = m_allowCustom ? customIndex() : m_currentIndex;
    }

    if (resolved != m_currentIndex) {
        m_currentIndex = resolved;
        Q_EMIT currentIndexChanged(resolved);
    }
}

int GeometrySelectorWidget::historyLength() const
{
    return m_historyLength;
}

void GeometrySelectorWidget::setHistoryLength(int length)
{
    const int clamped = std::max(0, length);
    if (clamped == m_historyLength) {
        return;
    }
    m_historyLength = clamped;
    truncateHistory();
    clampCurrentIndex();
    rebuildRows();
}

int GeometrySelectorWidget::historySize() const
{
    return static_cast<int>(m_history.size());
}

void GeometrySelectorWidget::setHistoryDataProvider(HistoryDataProvider provider)
{
    m_historyDataProvider = std::move(provider);
}

QVariant GeometrySelectorWidget::currentHistoryData() const
{
    const int offset = historyOffsetOf(m_currentIndex);
    return offset >= 0 ? m_history[offset].userData : QVariant {};
}

void GeometrySelectorWidget::captureHistoryEntry()
{
    if (!isComboMode() || m_historyLength <= 0 || m_selection->wasCancelled()) {
        return;
    }

    const std::vector<GeometryReference> picked = m_selection->references();
    // Nothing new to remember: either the session ended with nothing committed, or it ended
    // exactly where it began (e.g. Done pressed without picking anything further). A re-pick of
    // the very set the session started from is caught here too, so it is not additionally
    // promoted to the front of the history it may already be part of — its position is simply
    // left where it was.
    if (picked.empty() || referencesEqualAsSet(picked, m_referencesAtSessionStart)) {
        return;
    }
    const auto standsForPicked = [&picked](const GeometrySelectorOption& option) {
        return referencesEqualAsSet(option.references, picked);
    };
    if (std::ranges::any_of(m_options, standsForPicked)) {
        return;
    }

    GeometrySelectorOption entry = GeometrySelectorOption::fromReferences(picked);
    if (m_historyDataProvider) {
        entry.userData = m_historyDataProvider();
    }

    std::erase_if(m_history, standsForPicked);
    m_history.insert(m_history.begin(), std::move(entry));
    truncateHistory();

    // Set rather than applied: the references are already in place, and setCurrentIndex would
    // put them there again — and restart the pick if the index it replaced was Custom.
    m_currentIndex = static_cast<int>(m_options.size());
    Q_EMIT currentIndexChanged(m_currentIndex);
}

void GeometrySelectorWidget::forgetHistoryFor(const App::DocumentObject* deleted)
{
    const std::size_t before = m_history.size();
    std::erase_if(m_history, [deleted](const GeometrySelectorOption& entry) {
        return std::ranges::any_of(entry.references, [deleted](const GeometryReference& reference) {
            return reference.object == deleted;
        });
    });
    if (m_history.size() == before) {
        return;
    }

    // Re-derived rather than shifted: an entry that survived may have moved, and the references
    // are what says which one is current. Comparison only — nothing here reads through a
    // reference's object, which is what makes it safe to run from a deletion.
    reconcileIndexFromReferences();
    clampCurrentIndex();
    // Deliberately no rebuildRows(). The visible rows come from the selection's own references,
    // which this does not touch, and the dropdown is built fresh every time it opens — so there
    // is nothing to redraw. Rebuilding would instead re-read the references through the object
    // being deleted: App::Document emits this signal before the object is freed, so the read
    // would happen to work here and dangle everywhere else.
}

void GeometrySelectorWidget::truncateHistory()
{
    const auto limit = static_cast<std::size_t>(std::max(0, m_historyLength));
    if (m_history.size() > limit) {
        m_history.resize(limit);
    }
}

void GeometrySelectorWidget::clampCurrentIndex()
{
    if (m_currentIndex <= lastValidIndex()) {
        return;
    }
    m_currentIndex = m_allowCustom ? customIndex() : -1;
    Q_EMIT currentIndexChanged(m_currentIndex);
}

int GeometrySelectorWidget::lastValidIndex() const
{
    if (m_allowCustom) {
        return customIndex();
    }
    return static_cast<int>(m_options.size() + m_history.size()) - 1;
}

int GeometrySelectorWidget::historyOffsetOf(int index) const
{
    const int first = static_cast<int>(m_options.size());
    const int offset = index - first;
    return offset >= 0 && offset < historySize() ? offset : -1;
}

std::vector<GeometryReference> GeometrySelectorWidget::referencesForIndex(int index) const
{
    if (index >= 0 && index < static_cast<int>(m_options.size())) {
        return m_options[index].references;
    }
    if (const int offset = historyOffsetOf(index); offset >= 0) {
        return m_history[offset].references;
    }
    // The Custom entry, -1, and anything past the end alike: nothing to preview.
    return {};
}

void GeometrySelectorWidget::hoverOption(int index)
{
    m_selection->setHoveredReferences(referencesForIndex(index));
}

// ---------------------------------------------------------------------------
// Painting — frame drawn via ambient QStyle so every theme works.
// ---------------------------------------------------------------------------

void GeometrySelectorWidget::paintEvent(QPaintEvent* /*event*/)
{
    QStylePainter painter(this);

    // At rest in combo mode the whole control is a native combo box: the ambient QStyle paints the
    // frame and dropdown arrow, and — in the Option state — the selected option's label. Other
    // combo states render their own child rows (reference list, prompt) inside the edit-field area,
    // over this frame. Free-pick mode, and a combo mid-Custom-pick, use the list line-edit frame.
    if (rendersAsComboBox()) {
        paintAsComboBox(painter);
        return;
    }

    QStyleOptionFrame option;
    option.initFrom(this);  // carries real keyboard-focus and hover state
    option.state |= QStyle::State_Sunken;
    if (m_selection->isSelecting()) {
        option.state |= QStyle::State_HasFocus;  // stay "focused" through viewport picking
    }
    option.features = QStyleOptionFrame::None;
    option.lineWidth = m_frameThickness;
    painter.drawPrimitive(QStyle::PE_PanelLineEdit, option);
}

void GeometrySelectorWidget::paintAsComboBox(QStylePainter& painter) const
{
    QStyleOptionComboBox combo;
    combo.initFrom(this);  // real hover / focus state
    combo.rect = rect();
    combo.subControls = QStyle::SC_ComboBoxFrame | QStyle::SC_ComboBoxArrow;
    combo.frame = true;
    combo.iconSize = QSize(IconSize, IconSize);

    // Only the Option state shows a value in the frame itself; every other combo state leaves the
    // label empty and lets its child rows render the content over the frame.
    if (const GeometrySelectorOption* option = currentOption()) {
        combo.currentText = option->label;
        combo.currentIcon = option->icon;
    }

    // The complex control paints the frame + arrow; the label (icon + text) is a separate control
    // element, exactly as QComboBox::paintEvent drives it. An empty label draws nothing.
    painter.drawComplexControl(QStyle::CC_ComboBox, combo);

    // CE_ComboBoxLabel positions the icon/text from the style's own native edit-field inset
    // (SC_ComboBoxEditField) — the Select component's own padding, wider than the row inset
    // applyStyleMetrics() gives a reference row. Left uncorrected, the value would jump sideways
    // when the current entry switches between a predefined option (painted here) and a picked
    // reference (a ReferenceRow child widget). Shift the rect handed to CE_ComboBoxLabel by the
    // same delta, on every edge, so both the icon and the text land exactly where a row's icon and
    // text would.
    const QRect nativeEditField
        = style()->subControlRect(QStyle::CC_ComboBox, &combo, QStyle::SC_ComboBoxEditField, this);
    const QMargins rowContentInset = layout()->contentsMargins() + m_itemPadding;
    const QRect rowContentRect = rect().marginsRemoved(rowContentInset);

    QStyleOptionComboBox labelOption = combo;
    labelOption.rect = combo.rect.adjusted(
        rowContentRect.left() - nativeEditField.left(),
        rowContentRect.top() - nativeEditField.top(),
        rowContentRect.right() - nativeEditField.right(),
        rowContentRect.bottom() - nativeEditField.bottom()
    );
    painter.drawControl(QStyle::CE_ComboBoxLabel, labelOption);
}

void GeometrySelectorWidget::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    // Re-resolve token-driven metrics when the theme or style swaps out. rebuildRows() (which
    // calls applyStyleMetrics() itself) is needed rather than applyStyleMetrics() alone: it
    // re-derives the widget's own container metrics, but the rows already on screen bake their
    // icon-to-text spacing and item padding into their own QHBoxLayout at construction time —
    // a plain int handed to their constructor, not a live binding to the token — so without a
    // full rebuild they keep showing whatever the metrics were the last time a reference or
    // selection change last (re)built them.
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::FontChange) {
        rebuildRows();
    }
}

void GeometrySelectorWidget::mousePressEvent(QMouseEvent* event)
{
    // Accept the press in the states the widget paints itself — the empty prompt and the native
    // combo box — so the matching release is delivered here; in every other state the child rows
    // and their controls handle their own clicks.
    if (event->button() == Qt::LeftButton && widgetHandlesClick()) {
        // Take real keyboard focus so the frame shows its focused ring; the early return
        // otherwise skips the base class's click-to-focus handling.
        setFocus(Qt::MouseFocusReason);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void GeometrySelectorWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && widgetHandlesClick()) {
        activatePrimary();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

bool GeometrySelectorWidget::rendersAsComboBox() const
{
    return isComboMode() && !m_selection->isSelecting();
}

bool GeometrySelectorWidget::widgetHandlesClick() const
{
    // Free-pick empty state starts a pick on click; a combo-box appearance opens the popup on a
    // click a child row did not consume (e.g. the arrow or the padding around the value).
    return visualState() == VisualState::Empty || rendersAsComboBox();
}

void GeometrySelectorWidget::activatePrimary()
{
    if (isComboMode()) {
        openOptionsPopup();
    }
    else {
        m_selection->startSelecting();
    }
}

void GeometrySelectorWidget::openOptionsPopup()
{
    GeometrySelectorPopup* popup
        = new GeometrySelectorPopup(m_options, m_history, m_allowCustom, m_currentIndex, this);
    // Resized rather than fixed: the style widens a popup that grew a scroll bar, and trims one
    // whose last row is partly cut off, and a fixed width or height blocks both.
    popup->resize(width(), popup->sizeHint().height());
    popup->move(mapToGlobal(QPoint(0, height())));
    connect(popup, &GeometrySelectorPopup::optionActivated, this, [this, popup](int index) {
        onOptionActivated(index);
        popup->close();  // WA_DeleteOnClose frees the popup; also the outside-click/Escape path
    });
    connect(popup, &GeometrySelectorPopup::optionHovered, this, &GeometrySelectorWidget::hoverOption);
    popup->show();
}

void GeometrySelectorWidget::onOptionActivated(int index)
{
    // Re-choosing Custom while already at the Custom index must still restart the pick;
    // setCurrentIndex would no-op on an unchanged index.
    if (m_allowCustom && index == customIndex() && index == m_currentIndex) {
        m_selection->startSelecting();
        return;
    }
    setCurrentIndex(index);
}

// ---------------------------------------------------------------------------
// Style metrics — margins, spacing and fixed height sourced from tokens.
// ---------------------------------------------------------------------------

void GeometrySelectorWidget::applyStyleMetrics()
{
    QMargins itemPadding = FallbackItemPadding;
    int iconSpacing = FallbackSpacing;
    QMargins containerPadding;  // default {0,0,0,0}: frame is flush until a theme sets ListPadding
    int rowSpacing = 0;         // default: rows abut

    // Row metrics — item padding, icon spacing, inter-row gap and container padding — always come
    // from the List chain (via GeometrySelector→List), regardless of which mode is active: a
    // reference row must look identical in combo mode and free-pick mode, since only the chrome
    // around it (a real combo frame + arrow vs. a plain line-edit frame) differs. The line height
    // and frame thickness, by contrast, come from the *current* component's root box geometry —
    // GeometrySelector in free-pick mode, Select in combo mode — because those drive the control's
    // overall height, which in combo mode must match a sibling QComboBox, not a list. One line
    // follows the border-box model: frame + container padding + row content sum to the resolved
    // line height, so padding never grows the control.
    if (Application::Instance) {
        auto* fcStyle = Application::Instance->freeCADStyle();

        const StyleParameters::StyleContext rootContext = FreeCADStyle::contextOf(this);
        const FreeCADStyle::BoxGeometryDefinition rootGeometry = fcStyle->resolveBoxGeometry(
            rootContext
        );
        if (rootGeometry.minHeight) {
            m_lineHeight = *rootGeometry.minHeight;
        }
        const FreeCADStyle::BoxStyleDefinition rootStyle = fcStyle->resolveBoxStyle(rootContext);
        if (rootStyle.borderThickness) {
            m_frameThickness = qRound(rootStyle.borderThickness->top());
        }

        // Force the List chain for row/container metrics even when the "component" property is
        // temporarily "Select" (combo mode): Select carries no Item-level tokens of its own, and
        // its own root Padding is a button's, not a list's.
        StyleParameters::StyleContext listContext = rootContext;
        listContext.component = StyleParameters::StyleComponent::GeometrySelector;
        const FreeCADStyle::BoxGeometryDefinition listGeometry = fcStyle->resolveBoxGeometry(
            listContext
        );
        containerPadding = listGeometry.padding.toMargins();

        // contextOf only honours a non-Root element for recognised item-view widget types;
        // for this plain QWidget it leaves element=Root, so pin Item explicitly to reach the
        // ListItem* tokens (inherited via GeometrySelector→List) instead of the empty root.
        StyleParameters::StyleContext itemContext = listContext;
        itemContext.element = StyleParameters::StyleComponentElement::Item;
        const FreeCADStyle::BoxGeometryDefinition item = fcStyle->resolveBoxGeometry(itemContext);
        itemPadding = item.padding.toMargins();
        iconSpacing = item.iconSpacing;
        rowSpacing = item.spacing;
    }

    m_itemSpacing = iconSpacing;
    m_rowSpacing = rowSpacing;
    m_containerPadding = containerPadding;
    m_itemPadding = itemPadding;

    // Combo mode still paints a native combo box (frame + dropdown arrow via CC_ComboBox), but the
    // content it insets to is the same list-row band as free-pick mode: frame + ListPadding on
    // every side, so the highlighted row is inset and rounded exactly like a list row. Only the
    // right inset differs, reserving the arrow's own width — read from the style, never hardcoded —
    // plus a gap, so the row never runs under or flush against the chevron.
    if (rendersAsComboBox() && Application::Instance) {
        const int comboHeight = m_lineHeight > 0 ? m_lineHeight
                                                 : qMax(IconSize, fontMetrics().height());
        // The arrow's width does not depend on the rect passed in, but a generous nominal rect
        // keeps this query well-defined even before the widget has a real, laid-out size.
        constexpr int nominalWidth = 400;
        QStyleOptionComboBox combo;
        combo.initFrom(this);
        combo.frame = true;
        combo.rect = QRect(0, 0, nominalWidth, comboHeight);
        const int arrowWidth
            = style()->subControlRect(QStyle::CC_ComboBox, &combo, QStyle::SC_ComboBoxArrow, this).width();

        // No dedicated "gap before the arrow" token exists yet; ListItemIconSpacing is the
        // closest fit already resolved here — the same "gap between two adjacent parts of a row"
        // unit used for the icon-to-label gap. A theme author wanting a different chevron gap
        // would need a new token, e.g. GeometrySelectorArrowSpacing.
        const int arrowGap = m_itemSpacing;

        layout()->setContentsMargins(
            m_frameThickness + containerPadding.left(),
            m_frameThickness + containerPadding.top(),
            m_frameThickness + containerPadding.right() + arrowGap + arrowWidth,
            m_frameThickness + containerPadding.bottom()
        );
    }
    else {
        // Inset content by the frame border plus the container padding (ListPadding); each row
        // supplies its own item padding and ListItemSpacing the inter-row gap.
        layout()->setContentsMargins(
            m_frameThickness + containerPadding.left(),
            m_frameThickness + containerPadding.top(),
            m_frameThickness + containerPadding.right(),
            m_frameThickness + containerPadding.bottom()
        );
    }
    m_contentLayout->setSpacing(0);

    // Fill the field column like a line edit or combo box: Expanding horizontally so the
    // control stretches to the available width instead of hugging its content. Vertically
    // Fixed, since the height is driven entirely by the current state's content, so the parent
    // never stretches the widget when the task panel has spare vertical space.
    QSizePolicy policy = sizePolicy();
    policy.setHorizontalPolicy(QSizePolicy::Expanding);
    policy.setVerticalPolicy(QSizePolicy::Fixed);
    setSizePolicy(policy);
    // One line is the combo height in combo mode, otherwise the border-box height: the row content
    // plus the frame and container padding the outer layout insets on top and bottom. A
    // single-value selector is pinned to it; a multi-value one uses it as its one-row minimum.
    const int lineHeight = rendersAsComboBox() && m_lineHeight > 0 ? m_lineHeight
                                                                   : rowHeight()
            + (2 * m_frameThickness) + m_containerPadding.top() + m_containerPadding.bottom();
    if (m_selection->quantity() == GeometryQuantity::Single) {
        setFixedHeight(lineHeight);
    }
    else {
        setMinimumHeight(lineHeight);
        setMaximumHeight(QWIDGETSIZE_MAX);
    }
}

// ---------------------------------------------------------------------------
// Row building helpers
// ---------------------------------------------------------------------------

/// Returns the view-provider icon for the given object, or a null QIcon if
/// Gui::Application is not available or the object has no view provider.
static QIcon viewProviderIconFor(App::DocumentObject* object)
{
    if (!object) {
        return {};
    }
    if (!Gui::Application::Instance) {
        return {};
    }
    App::Document* appDoc = object->getDocument();
    if (!appDoc) {
        return {};
    }
    Gui::Document* guiDoc = Gui::Application::Instance->getDocument(appDoc);
    if (!guiDoc) {
        return {};
    }
    Gui::ViewProvider* viewProvider = guiDoc->getViewProvider(object);
    if (!viewProvider) {
        return {};
    }
    return viewProvider->getIcon();
}

/// Human-readable label for one reference: "Object" or "Object.Sub".
static QString referenceLabel(const GeometryReference& ref)
{
    const QString objectName = ref.object
        ? QString::fromStdString(ref.object->Label.getValue())
        : QCoreApplication::translate("Gui::GeometrySelectorWidget", "<deleted>");
    const QString subName = QString::fromStdString(ref.subName);
    return subName.isEmpty() ? objectName : objectName + u'.' + subName;
}

GeometrySelectorOption GeometrySelectorOption::fromReference(const GeometryReference& reference)
{
    return {
        .icon = viewProviderIconFor(reference.object),
        .label = referenceLabel(reference),
        .references = {reference},
        .userData = {},
    };
}

GeometrySelectorOption GeometrySelectorOption::fromReferences(std::vector<GeometryReference> references)
{
    const GeometryReference first = references.empty() ? GeometryReference {} : references.front();
    return {
        .icon = viewProviderIconFor(first.object),
        .label = referenceLabel(first),
        .references = std::move(references),
        .userData = {},
    };
}

GeometrySelectorOption GeometrySelectorOption::customEntry()
{
    GeometrySelectorOption option;
    option.label = QCoreApplication::translate("Gui::GeometrySelectorWidget", "Custom…");
    if (Gui::Application::Instance) {
        option.icon = IconManager::instance().icon(QStringLiteral(":/icons/tabler/outline/plus.svg"));
    }
    return option;
}

/// The selecting-state prompt: the "pick geometry" hint until something is committed, then a
/// running count so the user sees why the widget grows as references accumulate.
static QString selectingPromptText(int referenceCount)
{
    if (referenceCount == 0) {
        return QCoreApplication::translate("Gui::GeometrySelectorWidget", "Select sketch, face…");
    }
    // %n is Qt's numerus mechanism: a single source string that Qt Linguist expands into each
    // language's plural forms, picked at runtime from the count. Untranslated it renders the
    // source with %n substituted, hence the "(s)" placeholder for the source language.
    return QCoreApplication::translate(
        "Gui::GeometrySelectorWidget",
        "%n item(s) selected",
        nullptr,
        referenceCount
    );
}

namespace
{
/// A single reference row: type icon + elided label, plus a remove button that is
/// revealed only while the pointer is over this row. The row body neither highlights nor
/// changes cursor; a click on the body (not the remove button) invokes onActivate.
class ReferenceRow: public QWidget
{
public:
    ReferenceRow(
        const GeometryReference& reference,
        QMargins padding,
        int spacing,
        bool showRemove,
        std::function<void()> onActivate,
        std::function<void()> onRemove,
        std::function<void()> onHoverEnter,
        std::function<void()> onHoverLeave,
        QWidget* parent
    )
        : QWidget(parent)
        , m_activate(std::move(onActivate))
        , m_hoverEnter(std::move(onHoverEnter))
        , m_hoverLeave(std::move(onHoverLeave))
    {
        setObjectName(QStringLiteral("gsw_reference_row"));
        // Tag the row as a List row so it can paint the ListRow* hovered background itself.
        setProperty("component", "List");
        auto* rowLayout = new QHBoxLayout(this);
        rowLayout->setContentsMargins(padding);
        rowLayout->setSpacing(spacing);

        const QIcon icon = viewProviderIconFor(reference.object);
        if (!icon.isNull()) {
            auto* iconLabel = new QLabel(this);
            iconLabel->setPixmap(icon.pixmap(IconSize, IconSize));
            rowLayout->addWidget(iconLabel);
        }

        auto* label = new ElideLabel(this);
        label->setText(referenceLabel(reference));
        label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        // The icon-to-text gap already comes entirely from this row's layout spacing
        // (IconSpacing), matching the dropdown's itemViewLayout() exactly; ElideLabel's own
        // legacy 4px text inset would stack on top of it and widen the gap past the dropdown's.
        label->setTextInset(0);
        rowLayout->addWidget(label, 1);

        if (showRemove) {
            m_remove = makeActionButton(
                this,
                IconManager::instance().icon(":/icons/tabler/outline/trash.svg")
            );
            m_remove->setToolTip(QCoreApplication::translate("Gui::GeometrySelectorWidget", "Remove"));
            styleAsLinkButton(m_remove);
            m_remove->hide();
            QObject::connect(m_remove, &QToolButton::clicked, this, [handler = std::move(onRemove)] {
                handler();
            });
            rowLayout->addWidget(m_remove);
        }
    }

protected:
    void enterEvent(QEnterEvent* /*event*/) override
    {
        m_hovered = true;
        update();  // repaint with the hovered row background
        if (m_remove) {
            m_remove->show();
        }
        if (m_hoverEnter) {
            m_hoverEnter();
        }
    }
    void leaveEvent(QEvent* /*event*/) override
    {
        m_hovered = false;
        update();
        if (m_remove) {
            m_remove->hide();
        }
        if (m_hoverLeave) {
            m_hoverLeave();
        }
    }
    void paintEvent(QPaintEvent* /*event*/) override
    {
        // The row is transparent at rest; only the hovered state paints a background, drawn
        // through the shared List box painting so it matches the tree/list delegates.
        if (!m_hovered || !Application::Instance) {
            return;
        }
        StyleParameters::StyleContext context = FreeCADStyle::contextOf(this);
        context.element = StyleParameters::StyleComponentElement::Row;
        context.state |= StyleParameters::StyleState::Hovered;
        QPainter painter(this);
        Application::Instance->freeCADStyle()->paintBox(&painter, rect(), context);
    }
    void mousePressEvent(QMouseEvent* event) override
    {
        // Accept the press so the matching release is delivered to this row; the child
        // labels do not accept events, so without this the release could be lost.
        if (event->button() == Qt::LeftButton) {
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        // A click anywhere on the row body (not consumed by the remove button) re-selects.
        if (event->button() == Qt::LeftButton && m_activate) {
            m_activate();
        }
    }

private:
    QToolButton* m_remove = nullptr;
    std::function<void()> m_activate;
    std::function<void()> m_hoverEnter;
    std::function<void()> m_hoverLeave;
    bool m_hovered = false;
};

/// The idle-state prompt. Its plus icon and label are laid out exactly like a committed
/// reference row — inset by the item padding, with the item icon spacing between them — so the
/// prompt aligns with the rows that will replace it. Its *look* is an InternalButton, though:
/// transparent at rest, and on hover the whole row fills with the InternalButton background
/// (its colour and radius), not the list-row highlight. A click starts selecting. Painted
/// directly rather than hosting a QToolButton so the label stays left-aligned.
class PromptButton: public QWidget
{
public:
    PromptButton(
        QString text,
        QMargins itemPadding,
        int iconSpacing,
        std::function<void()> onActivate,
        QWidget* parent
    )
        : QWidget(parent)
        , m_text(std::move(text))
        , m_itemPadding(itemPadding)
        , m_iconSpacing(iconSpacing)
        , m_activate(std::move(onActivate))
    {
        setObjectName(QStringLiteral("gsw_prompt"));
        // The InternalButton token chain drives only the hovered background style; the layout
        // and size come from the item padding, matching a reference row.
        setProperty("component", "InternalButton");
    }

    // Report the label-plus-padding extent as the minimum so the field keeps a sensible
    // minimum width instead of collapsing; beyond that the control expands to fill the column.
    QSize sizeHint() const override
    {
        return contentExtent();
    }
    QSize minimumSizeHint() const override
    {
        return contentExtent();
    }

protected:
    void enterEvent(QEnterEvent* /*event*/) override
    {
        m_hovered = true;
        update();
    }
    void leaveEvent(QEvent* /*event*/) override
    {
        m_hovered = false;
        update();
    }
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter painter(this);
        // Transparent at rest; on hover the whole row fills with the InternalButton background
        // style (its colour and radius) so the prompt reads as an internal button while occupying
        // the same extent as a committed reference row.
        if (m_hovered && Application::Instance) {
            StyleParameters::StyleContext context = FreeCADStyle::contextOf(this);
            context.state |= StyleParameters::StyleState::Hovered;
            Application::Instance->freeCADStyle()->paintBox(&painter, rect(), context);
        }

        // Lay the icon and label out like a reference row: inset from the row by the item padding,
        // with the item icon spacing between them.
        const QColor foreground = palette().color(QPalette::PlaceholderText);
        QRect content = rect().marginsRemoved(m_itemPadding);

        const QPixmap icon = IconManager::instance().pixmap(
            QStringLiteral(":/icons/tabler/outline/plus.svg"),
            QSize(IconSize, IconSize),
            foreground
        );
        if (!icon.isNull()) {
            const QRect iconRect(
                content.left(),
                content.top() + ((content.height() - IconSize) / 2),
                IconSize,
                IconSize
            );
            painter.drawPixmap(iconRect, icon);
            content.setLeft(iconRect.right() + 1 + m_iconSpacing);
        }

        painter.setPen(foreground);
        painter.drawText(content, Qt::AlignLeft | Qt::AlignVCenter, m_text);
    }
    void mousePressEvent(QMouseEvent* event) override
    {
        // Accept the press so the matching release lands here and can start selecting.
        if (event->button() == Qt::LeftButton) {
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && m_activate) {
            m_activate();
        }
    }

private:
    /// The intrinsic extent — icon and label plus the item padding — reported only as the
    /// minimum/hint size; the row expands to fill the column beyond it, exactly like a
    /// reference row.
    QSize contentExtent() const
    {
        const QSize label = fontMetrics().size(Qt::TextSingleLine, m_text);
        return {
            m_itemPadding.left() + m_itemPadding.right() + IconSize + m_iconSpacing + label.width(),
            m_itemPadding.top() + m_itemPadding.bottom() + qMax(IconSize, label.height())
        };
    }

    QString m_text;
    QMargins m_itemPadding;
    int m_iconSpacing;
    std::function<void()> m_activate;
    bool m_hovered = false;
};

/// The selecting-state chrome: a scrim that dims whatever sits beneath it in a StackAll
/// QStackedLayout, with the italic prompt filling the padded area and the Done/Cancel action
/// buttons positioned absolutely over the right edge — vertically centred in the full overlay
/// height and outside the layout flow, so their taller internal size never dictates the
/// overlay's height. Opaque to mouse events so clicks never reach the dimmed content beneath.
class SelectingOverlay: public QWidget
{
public:
    SelectingOverlay(
        const QString& promptText,
        QMargins padding,
        int spacing,
        bool showConfirm,
        std::function<void()> onDone,
        std::function<void()> onCancel,
        QWidget* parent
    )
        : QWidget(parent)
        , m_padding(padding)
        , m_spacing(spacing)
    {
        setObjectName(QStringLiteral("gsw_overlay"));
        setAttribute(Qt::WA_NoSystemBackground);  // we paint our own scrim
        setAutoFillBackground(false);

        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(padding);
        layout->setSpacing(spacing);

        auto* prompt = new QLabel(promptText, this);
        QFont italicFont = prompt->font();
        italicFont.setItalic(true);
        prompt->setFont(italicFont);
        prompt->setForegroundRole(QPalette::PlaceholderText);
        prompt->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        prompt->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        layout->addWidget(prompt, 1);

        // Done commits an accumulated Ctrl-multiselect; only the multi-select mode can
        // accumulate, so the caller asks for it only then. The accent Primary variant is
        // carried via the buttonType property inside makeInternalTextButton.
        if (showConfirm) {
            addFloatingButton(
                QCoreApplication::translate("Gui::GeometrySelectorWidget", "Done"),
                /*primary=*/true,
                std::move(onDone),
                QStringLiteral("gsw_confirm")
            );
        }
        // Cancel reverts the session; a neutral chromed internal-size tool button beside Done.
        addFloatingButton(
            QCoreApplication::translate("Gui::GeometrySelectorWidget", "Cancel"),
            /*primary=*/false,
            std::move(onCancel),
            QStringLiteral("gsw_cancel")
        );
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        layoutFloatingButtons();
    }

    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter painter(this);
        QColor scrim = palette().color(QPalette::Base);
        scrim.setAlphaF(static_cast<float>(ScrimOpacity));  // dim the list, keep it faintly visible
        painter.fillRect(rect(), scrim);
    }

private:
    /// Creates a chromed internal-size button that floats over the overlay instead of joining
    /// the layout, so its height never contributes to the overlay's size hint.
    void addFloatingButton(
        const QString& text,
        bool primary,
        std::function<void()> onClick,
        const QString& name
    )
    {
        auto* button = makeInternalTextButton(this, text, primary);
        button->setObjectName(name);
        QObject::connect(button, &QToolButton::clicked, this, [handler = std::move(onClick)] {
            handler();
        });
        m_buttons.push_back(button);
    }

    /// Right-aligns the floating buttons from the padded right edge inwards, each vertically
    /// centred in the overlay. The height is clamped to the overlay so a button taller than a
    /// single row is bounded by the frame instead of protruding past it — the buttons are
    /// positioned absolutely and never dictate (nor overflow) the widget height.
    void layoutFloatingButtons()
    {
        int rightEdge = width() - m_padding.right();
        for (std::size_t index = m_buttons.size(); index-- > 0;) {
            QToolButton* button = m_buttons[index];
            const QSize hint = button->sizeHint();
            const int buttonHeight = qMin(hint.height(), height());
            const int left = rightEdge - hint.width();
            const int top = (height() - buttonHeight) / 2;
            button->setGeometry(left, top, hint.width(), buttonHeight);
            rightEdge = left - m_spacing;
        }
    }

    QMargins m_padding;
    int m_spacing;
    std::vector<QToolButton*> m_buttons;
};
}  // namespace

QWidget* GeometrySelectorWidget::makeEmptyRow()
{
    // The idle prompt fills the row and handles its own hover and clicks: transparent at rest,
    // a light gray InternalButton box on hover, and a click starts selecting.
    auto* prompt = new PromptButton(
        tr("Select geometry"),
        m_itemPadding,
        m_itemSpacing,
        [this] { activatePrimary(); },
        this
    );
    prompt->setFixedHeight(rowHeight());
    return prompt;
}

QWidget* GeometrySelectorWidget::makeReferenceList()
{
    const std::vector<GeometryReference>& references = m_selection->references();
    const int singleRowHeight = rowHeight();

    auto* rowsContainer = new QWidget;
    auto* rowsLayout = new QVBoxLayout(rowsContainer);
    rowsLayout->setContentsMargins(0, 0, 0, 0);
    // Inter-row gap from ListItemSpacing; 0 (default) leaves rows abutting.
    rowsLayout->setSpacing(m_rowSpacing);

    for (std::size_t index = 0; index < references.size(); ++index) {
        auto* referenceRow = new ReferenceRow(
            references[index],
            m_itemPadding,
            m_itemSpacing,
            // A predefined option never reaches makeReferenceList() (it renders through
            // paintAsComboBox() instead — see visualState()), so every row built here already
            // stands for a picked reference (custom or history), never a predefined option's own
            // bundled references. Spelled out explicitly rather than inferred from isComboMode(),
            // which was true for a predefined multi-reference option too and hid the button there
            // regardless of mode: removing a picked reference is meaningful, removing a predefined
            // option's geometry is not — there is nothing else for the control to fall back to.
            /*showRemove=*/currentOption() == nullptr,
            [this] { activatePrimary(); },
            [this, index] { m_selection->removeReference(index); },
            [this, index] { m_selection->setHoveredReference(static_cast<int>(index)); },
            [this] { m_selection->setHoveredReference(-1); },
            rowsContainer
        );
        // Pin every row to the resolved row height so the list matches other list-like
        // controls and does not stretch its rows apart to fill spare vertical space.
        referenceRow->setFixedHeight(singleRowHeight);
        rowsLayout->addWidget(referenceRow);
    }

    // While the rows fit, lay them out directly: the container sizes exactly to its rows,
    // matching the neighbouring form controls. A QScrollArea is only introduced once the
    // rows overflow, because it defaults to Expanding and imposes a minimum-size floor
    // that would otherwise inflate a short list.
    if (static_cast<int>(references.size()) <= MaxRowsWithoutScroll) {
        return rowsContainer;
    }

    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("gsw_reference_list"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(rowsContainer);

    // Cap the viewport so a partial row peeks and the list starts scrolling; QScrollArea
    // never collapses to its content on its own, so pin the height explicitly.
    scroll->setFixedHeight(referenceListHeight());
    return scroll;
}

QWidget* GeometrySelectorWidget::makeSelecting()
{
    auto* container = new QWidget(this);
    auto* stack = new QStackedLayout(container);
    stack->setStackingMode(QStackedLayout::StackAll);
    stack->setContentsMargins(0, 0, 0, 0);

    // The committed references sit dimmed beneath the overlay; reuse the list builder so the
    // rows are pixel-identical to the idle state. With nothing committed there is nothing to
    // dim, so the backdrop covers the bare frame rather than an idle placeholder row.
    const bool hasReferences = !m_selection->references().empty();
    if (hasReferences) {
        stack->addWidget(makeReferenceList());
    }

    auto* overlay = new SelectingOverlay(
        selectingPromptText(static_cast<int>(m_selection->references().size())),
        m_itemPadding,
        m_itemSpacing,
        /*showConfirm=*/m_selection->quantity() == GeometryQuantity::AllowMultiple,
        [this] { m_selection->stopSelecting(); },
        [this] { m_selection->cancelSelecting(); },
        container
    );
    stack->addWidget(overlay);
    stack->setCurrentWidget(overlay);  // topmost in StackAll

    // A QScrollArea reports its full content as its size hint regardless of its fixed height,
    // so the stack would otherwise grow to every row. Pin the container to the capped list
    // height (or a single row when there is nothing to list) so the selecting state keeps the
    // idle list's height.
    container->setFixedHeight(hasReferences ? referenceListHeight() : rowHeight());
    return container;
}

int GeometrySelectorWidget::rowHeight() const
{
    // Border-box, in both modes: one line (m_lineHeight) is the frame, the container padding and
    // the row content combined, so the content shrinks as ListPadding grows and every selector
    // stays exactly one line height tall regardless of its padding. In combo mode m_lineHeight is
    // the control's native combo height and m_frameThickness/m_containerPadding are the same
    // frame + ListPadding the content margins use, so this yields exactly the row band left inside
    // them — no separate combo-only branch needed.
    if (m_lineHeight > 0) {
        return m_lineHeight - (2 * m_frameThickness)
            - (m_containerPadding.top() + m_containerPadding.bottom());
    }
    // Headless fallback (no FreeCADStyle): size the row to its content plus the item padding.
    return qMax(IconSize, fontMetrics().height()) + m_itemPadding.top() + m_itemPadding.bottom();
}

int GeometrySelectorWidget::referenceListHeight() const
{
    const int rowCount = static_cast<int>(m_selection->references().size());
    // Full content height: rows plus one gap between each adjacent pair.
    const int fullHeight = (rowCount * rowHeight()) + (qMax(0, rowCount - 1) * m_rowSpacing);
    // Cap so a partial row peeks; include the gaps between the visible rows.
    const int visibleGapCount = static_cast<int>(MaxVisibleRows);
    const int cappedHeight = qRound(MaxVisibleRows * rowHeight()) + (visibleGapCount * m_rowSpacing);
    return qMin(fullHeight, cappedHeight);
}

// ---------------------------------------------------------------------------
// Row management
// ---------------------------------------------------------------------------

void GeometrySelectorWidget::clearRows()
{
    while (QLayoutItem* item = m_contentLayout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            // Detach immediately so a rebuild triggered from a descendant's own event
            // handler (e.g. this row's remove button) never observes stale rows; the
            // actual C++ deletion is deferred to stay safe for that same reentrant case.
            widget->setParent(nullptr);
            widget->deleteLater();
        }
        delete item;
    }
}

GeometrySelectorWidget::VisualState GeometrySelectorWidget::visualState() const
{
    if (m_selection->isSelecting()) {
        return VisualState::Selecting;
    }
    // A current predefined option (logical or reference-bearing) reads as a Select Box value:
    // show its icon + label rather than the empty prompt or the bare reference rows.
    if (isComboMode() && currentOption() != nullptr) {
        return VisualState::Option;
    }
    return m_selection->references().empty() ? VisualState::Empty : VisualState::ReferenceList;
}

void GeometrySelectorWidget::rebuildRows()
{
    clearRows();

    const VisualState state = visualState();

    // A resting combo is a native combo box (Select component); free-pick mode — and a combo
    // during its Custom pick — use the list-styled GeometrySelector frame. Switching the component
    // re-derives the padding, height and arrow reserve, so nothing is stale from a previous state.
    setProperty("component", rendersAsComboBox() ? "Select" : "GeometrySelector");
    applyStyleMetrics();

    switch (state) {
        case VisualState::Empty:
            m_contentLayout->addWidget(makeEmptyRow());
            break;
        case VisualState::Option:
            // Painted natively as a combo box in paintEvent; the frame owns the whole row, so
            // there is no child widget — clicks are handled by the widget itself.
            break;
        case VisualState::Selecting:
            m_contentLayout->addWidget(makeSelecting());
            break;
        case VisualState::ReferenceList:
            m_contentLayout->addWidget(makeReferenceList());
            break;
    }

    update();
}
