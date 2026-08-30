// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QStatusBar>

#include <FCGlobal.h>

#include "StyleParameters/StyleContext.h"

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

private:
    /// The band the message is drawn in: everything left of the first item that stays visible.
    QRect messageRect() const;

    StyleParameters::MessageLevel _messageLevel = StyleParameters::MessageLevel::Default;
};

}  // namespace Gui
