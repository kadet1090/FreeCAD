// SPDX-License-Identifier: LGPL-2.1-or-later
/****************************************************************************
 *                                                                          *
 *   Copyright (c) 2025 Kacper Donat <kacper@kadet.net>                     *
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

#include "PreCompiled.h"
#ifndef _PreComp_
# include <QBuffer>
# include <QPainter>
#endif

#include <BitmapFactory.h>

#include "InputHint.h"
#include "InputHintWidget.h"

Gui::InputHintWidget::InputHintWidget(QWidget* parent) : QLabel(parent)
{}

void Gui::InputHintWidget::showHints(const std::list<InputHint>& hints)
{
    if (hints.empty()) {
        clearHints();
        return;
    }

    const auto getKeyImage = [this](InputHint::UserInput key) {
        const auto& factory = BitmapFactory();

        QPixmap image = [&] {
            QColor color = palette().text().color();

            if (auto iconPath = getCustomIconPath(key)) {
                return factory.pixmapFromSvg(*iconPath,
                                             QSize(24, 24),
                                             {{0xFFFFFF, color.rgb() & RGB_MASK}});
            }

            return generateKeyIcon(key, color);
        }();


        QBuffer buffer;
        image.save(&buffer, "png");

        return QStringLiteral("<img src=\"data:image/png;base64,%1\" width=%2 height=24 />")
            .arg(QLatin1String(buffer.data().toBase64()))
            .arg(image.width());
    };

    const auto getHintHTML = [&](const InputHint& hint) {
        QString message = QStringLiteral("<td valign=bottom>%1</td>").arg(hint.message);

        for (const auto& sequence : hint.sequences) {
            QList<QString> keyImages;

            for (const auto key : sequence.keys) {
                keyImages.append(getKeyImage(key));
            }

            message = message.arg(keyImages.join(QString {}));
        }

        return message;
    };

    QStringList messages;
    for (const auto& hint : hints) {
        messages.append(getHintHTML(hint));
    }

    QString html = QStringLiteral("<table style=\"line-height: 28px\" height=28>"
                                  "<tr>%1</tr>"
                                  "</table>");

    setText(html.arg(messages.join(QStringLiteral("<td width=10></td>"))));
}

void Gui::InputHintWidget::clearHints()
{
    setText({});
}

std::optional<const char*> Gui::InputHintWidget::getCustomIconPath(const InputHint::UserInput key)
{
    switch (key) {
        case InputHint::UserInput::MouseLeft:
            return ":/icons/user-input/mouse-left.svg";
        case InputHint::UserInput::MouseRight:
            return ":/icons/user-input/mouse-right.svg";
        case InputHint::UserInput::MouseMove:
            return ":/icons/user-input/mouse-move.svg";
        case InputHint::UserInput::MouseMiddle:
            return ":/icons/user-input/mouse-middle.svg";
        case InputHint::UserInput::MouseScroll:
            return ":/icons/user-input/mouse-scroll.svg";
        case InputHint::UserInput::MouseScrollDown:
            return ":/icons/user-input/mouse-scroll-down.svg";
        case InputHint::UserInput::MouseScrollUp:
            return ":/icons/user-input/mouse-scroll-up.svg";
        default:
            return std::nullopt;
    }
}

QPixmap Gui::InputHintWidget::generateKeyIcon(const InputHint::UserInput key, const QColor color)
{
    constexpr int margin = 3;
    constexpr int padding = 4;
    constexpr int radius = 2;
    constexpr int iconTotalHeight = 24;
    constexpr int iconSymbolHeight = iconTotalHeight - 2 * margin;

    const QFont font(QStringLiteral("sans"), 10, QFont::Bold);
    const QFontMetrics fm(font);
    const QString text = inputRepresentation(key);
    const QRect textBoundingRect = fm.tightBoundingRect(text);

    const int symbolWidth = std::max(textBoundingRect.width() + padding * 2, iconSymbolHeight);

    const QRect keyRect(margin, margin, symbolWidth, 18);

    QPixmap pixmap(symbolWidth + margin * 2, iconTotalHeight);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(color, 2));
    painter.setFont(font);
    painter.drawRoundedRect(keyRect, radius, radius);
    painter.drawText(
        // adjust the rectangle so it is visually centered
        // this is important for characters that are below baseline
        keyRect.translated(0, -(textBoundingRect.y() + textBoundingRect.height()) / 2),
        Qt::AlignHCenter,
        text);

    return pixmap;
}

QString Gui::InputHintWidget::inputRepresentation(const InputHint::UserInput key)
{
    using Hint = Gui::InputHint::UserInput;

    // clang-format off
    switch (key) {
        // Keyboard Keys
        case Hint::KeySpace: return tr("Space");
        case Hint::KeyExclam: return QStringLiteral("!");
        case Hint::KeyQuoteDbl: return QStringLiteral("\"");
        case Hint::KeyNumberSign: return QStringLiteral("-/+");
        case Hint::KeyDollar: return QStringLiteral("$");
        case Hint::KeyPercent: return QStringLiteral("%");
        case Hint::KeyAmpersand: return QStringLiteral("&");
        case Hint::KeyApostrophe: return QStringLiteral("\'");
        case Hint::KeyParenLeft: return QStringLiteral("(");
        case Hint::KeyParenRight: return QStringLiteral(")");
        case Hint::KeyAsterisk: return QStringLiteral("*");
        case Hint::KeyPlus: return QStringLiteral("+");
        case Hint::KeyComma: return QStringLiteral(",");
        case Hint::KeyMinus: return QStringLiteral("-");
        case Hint::KeyPeriod: return QStringLiteral(".");
        case Hint::KeySlash: return QStringLiteral("/");
        case Hint::Key0: return QStringLiteral("0");
        case Hint::Key1: return QStringLiteral("1");
        case Hint::Key2: return QStringLiteral("2");
        case Hint::Key3: return QStringLiteral("3");
        case Hint::Key4: return QStringLiteral("4");
        case Hint::Key5: return QStringLiteral("5");
        case Hint::Key6: return QStringLiteral("6");
        case Hint::Key7: return QStringLiteral("7");
        case Hint::Key8: return QStringLiteral("8");
        case Hint::Key9: return QStringLiteral("9");
        case Hint::KeyColon: return QStringLiteral(":");
        case Hint::KeySemicolon: return QStringLiteral(";");
        case Hint::KeyLess: return QStringLiteral("<");
        case Hint::KeyEqual: return QStringLiteral("=");
        case Hint::KeyGreater: return QStringLiteral(">");
        case Hint::KeyQuestion: return QStringLiteral("?");
        case Hint::KeyAt: return QStringLiteral("@");
        case Hint::KeyA: return QStringLiteral("A");
        case Hint::KeyB: return QStringLiteral("B");
        case Hint::KeyC: return QStringLiteral("C");
        case Hint::KeyD: return QStringLiteral("D");
        case Hint::KeyE: return QStringLiteral("E");
        case Hint::KeyF: return QStringLiteral("F");
        case Hint::KeyG: return QStringLiteral("G");
        case Hint::KeyH: return QStringLiteral("H");
        case Hint::KeyI: return QStringLiteral("I");
        case Hint::KeyJ: return QStringLiteral("J");
        case Hint::KeyK: return QStringLiteral("K");
        case Hint::KeyL: return QStringLiteral("L");
        case Hint::KeyM: return QStringLiteral("M");
        case Hint::KeyN: return QStringLiteral("N");
        case Hint::KeyO: return QStringLiteral("O");
        case Hint::KeyP: return QStringLiteral("P");
        case Hint::KeyQ: return QStringLiteral("Q");
        case Hint::KeyR: return QStringLiteral("R");
        case Hint::KeyS: return QStringLiteral("S");
        case Hint::KeyT: return QStringLiteral("T");
        case Hint::KeyU: return QStringLiteral("U");
        case Hint::KeyV: return QStringLiteral("V");
        case Hint::KeyW: return QStringLiteral("W");
        case Hint::KeyX: return QStringLiteral("X");
        case Hint::KeyY: return QStringLiteral("Y");
        case Hint::KeyZ: return QStringLiteral("Z");
        case Hint::KeyBracketLeft: return QStringLiteral("[");
        case Hint::KeyBackslash: return QStringLiteral("\\");
        case Hint::KeyBracketRight: return QStringLiteral("]");
        case Hint::KeyUnderscore: return QStringLiteral("_");
        case Hint::KeyQuoteLeft: return QStringLiteral("\"");
        case Hint::KeyBraceLeft: return QStringLiteral("{");
        case Hint::KeyBar: return QStringLiteral("|");
        case Hint::KeyBraceRight: return QStringLiteral("}");
        case Hint::KeyAsciiTilde: return QStringLiteral("~");

        // misc keys
        case Hint::KeyEscape: return tr("Escape");
        case Hint::KeyTab: return tr("tab ⭾");
        case Hint::KeyBacktab: return tr("Backtab");
        case Hint::KeyBackspace: return tr("⌫");
        case Hint::KeyReturn: return tr("↵ Enter");
        case Hint::KeyEnter: return tr("Enter");
        case Hint::KeyInsert: return tr("Insert");
        case Hint::KeyDelete: return tr("Delete");
        case Hint::KeyPause: return tr("Pause");
        case Hint::KeyPrintScr: return tr("Print");
        case Hint::KeySysReq: return tr("SysReq");
        case Hint::KeyClear: return tr("Clear");

        // cursor movement
        case Hint::KeyHome: return tr("Home");
        case Hint::KeyEnd: return tr("End");
        case Hint::KeyLeft: return QStringLiteral("←");
        case Hint::KeyUp: return QStringLiteral("↑");
        case Hint::KeyRight: return QStringLiteral("→");
        case Hint::KeyDown: return QStringLiteral("↓");
        case Hint::KeyPageUp: return tr("PgDown");
        case Hint::KeyPageDown: return tr("PgUp");

        // modifiers
#ifdef FC_OS_MACOSX
        case Hint::KeyShift: return QStringLiteral("⇧");
        case Hint::KeyControl: return QStringLiteral("⌘");
        case Hint::KeyMeta: return QStringLiteral("⌃");
        case Hint::KeyAlt: return QStringLiteral("⌥");
#else
        case Hint::KeyShift: return tr("Shift");
        case Hint::KeyControl: return tr("Ctrl");
#ifdef FC_OS_WIN32
        case Hint::KeyMeta: return tr("⊞ Win");
#else
        case Hint::KeyMeta: return tr("❖ Meta");
#endif
        case Hint::KeyAlt: return tr("Alt");
#endif
        case Hint::KeyCapsLock: return tr("Caps Lock");
        case Hint::KeyNumLock: return tr("Num Lock");
        case Hint::KeyScrollLock: return tr("Scroll Lock");

        // function
        case Hint::KeyF1: return QStringLiteral("F1");
        case Hint::KeyF2: return QStringLiteral("F2");
        case Hint::KeyF3: return QStringLiteral("F3");
        case Hint::KeyF4: return QStringLiteral("F4");
        case Hint::KeyF5: return QStringLiteral("F5");
        case Hint::KeyF6: return QStringLiteral("F6");
        case Hint::KeyF7: return QStringLiteral("F7");
        case Hint::KeyF8: return QStringLiteral("F8");
        case Hint::KeyF9: return QStringLiteral("F9");
        case Hint::KeyF10: return QStringLiteral("F10");
        case Hint::KeyF11: return QStringLiteral("F11");
        case Hint::KeyF12: return QStringLiteral("F12");
        case Hint::KeyF13: return QStringLiteral("F13");
        case Hint::KeyF14: return QStringLiteral("F14");
        case Hint::KeyF15: return QStringLiteral("F15");
        case Hint::KeyF16: return QStringLiteral("F16");
        case Hint::KeyF17: return QStringLiteral("F17");
        case Hint::KeyF18: return QStringLiteral("F18");
        case Hint::KeyF19: return QStringLiteral("F19");
        case Hint::KeyF20: return QStringLiteral("F20");
        case Hint::KeyF21: return QStringLiteral("F21");
        case Hint::KeyF22: return QStringLiteral("F22");
        case Hint::KeyF23: return QStringLiteral("F23");
        case Hint::KeyF24: return QStringLiteral("F24");
        case Hint::KeyF25: return QStringLiteral("F25");
        case Hint::KeyF26: return QStringLiteral("F26");
        case Hint::KeyF27: return QStringLiteral("F27");
        case Hint::KeyF28: return QStringLiteral("F28");
        case Hint::KeyF29: return QStringLiteral("F29");
        case Hint::KeyF30: return QStringLiteral("F30");
        case Hint::KeyF31: return QStringLiteral("F31");
        case Hint::KeyF32: return QStringLiteral("F32");
        case Hint::KeyF33: return QStringLiteral("F33");
        case Hint::KeyF34: return QStringLiteral("F34");
        case Hint::KeyF35: return QStringLiteral("F35");

        default: return tr("???");
    }
    // clang-format on
}
