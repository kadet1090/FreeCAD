// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <QMenuBar>

namespace Gui
{

/**
 * Custom QMenuBar subclass that:
 *  - Draws all items without the per-item clip that Qt's default paintEvent applies,
 *    so CE_MenuBarItem can paint a highlight that spans the full bar height.
 *  - Forces corner widget geometries to fill the full bar height edge-to-edge after
 *    every resize or action change, overriding Qt's default vmargin inset.
 */
class FCMenuBar: public QMenuBar
{
    Q_OBJECT
public:
    explicit FCMenuBar(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
};

}  // namespace Gui
