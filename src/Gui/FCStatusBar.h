// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <optional>

#include <QPointer>
#include <QStatusBar>

#include <FCGlobal.h>

#include "StyleParameters/StyleContext.h"

class QLayout;

namespace Gui
{

/**
 * @brief The main window's status bar, whose transient message is coloured by its severity.
 *
 * Qt paints that message with the bar's own window-text colour, which every child label reads
 * as well; this bar paints it from the StatusBar tokens instead, so a warning colours the
 * message without colouring the items beside it.
 *
 * The severity is stated rather than derived: it comes from the log level that produced the
 * message, which no style option carries.
 */
class GuiExport FCStatusBar: public QStatusBar
{
    Q_OBJECT

public:
    explicit FCStatusBar(QWidget* parent = nullptr);

    void setMessageLevel(StyleParameters::MessageLevel level);

    StyleParameters::MessageLevel messageLevel() const
    {
        return _messageLevel;
    }

protected:
    void paintEvent(QPaintEvent* event) override;
    bool event(QEvent* event) override;

private:
    /// The band the message is drawn in: everything left of the first item that stays visible.
    QRect messageRect() const;

    /// Writes the stated inset and item gap onto the layout Qt rebuilt, which reads no metric.
    void applyLayoutTokens();

    /// Counts @p stated and what the layout now needs into the bar's own floor, which sizes it.
    void applyHeightFloor(std::optional<int> stated);

    StyleParameters::MessageLevel _messageLevel = StyleParameters::MessageLevel::Default;

    /// The layout last written to, so a request that rebuilt nothing is not answered with a write.
    QPointer<QLayout> _adjustedLayout;
};

}  // namespace Gui
