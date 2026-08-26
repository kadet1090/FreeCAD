// SPDX-License-Identifier: LGPL-2.1-or-later
/****************************************************************************
 *                                                                          *
 *   Copyright (c) 2025 Alfredo Monclus <alfredomonclus@gmail.com>          *
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

// This custom widget adds the missing ellipsize functionality in QT5

#pragma once

#include <QLabel>
#include <QPainter>
#include <QFontMetrics>

#include <FCGlobal.h>

namespace Gui
{

class GuiExport ElideLabel: public QLabel
{
    Q_OBJECT

public:
    explicit ElideLabel(QWidget* parent = nullptr);
    ~ElideLabel() override = default;

    /// Horizontal inset applied to the text on both sides at paint time. Defaults to 4px, the
    /// value this label always hardcoded, kept only so existing consumers keep rendering
    /// byte-identically. It exists purely for backwards compatibility: a consumer whose
    /// surrounding layout already supplies its own icon/text spacing should set this to 0, and
    /// once every consumer does that this property — and the default — can go away.
    int textInset() const
    {
        return m_textInset;
    }
    void setTextInset(int inset);

protected:
    void paintEvent(QPaintEvent* event) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    int m_textInset = 4;
};

}  // namespace Gui
