/***************************************************************************
 *   Copyright (c) 2007 Werner Mayer <wmayer[at]users.sourceforge.net>     *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/


#ifndef GUI_CALLTIPS_H
#define GUI_CALLTIPS_H

#include <QListWidget>

class QPlainTextEdit;

namespace Py
{
class Object;
class List;
class String;
}  // namespace Py

namespace Gui
{

class CallTip
{
public:
    enum Type
    {
        Unknown,
        Module,
        Class,
        Method,
        Member,
        Property
    };
    QString name;
    QString description;
    QString parameter;
    Type type {Unknown};
};

/**
 * @author Werner Mayer
 */
class CallTipsList: public QListWidget
{
    Q_DISABLE_COPY_MOVE(CallTipsList)
    Q_OBJECT

public:
    /// Construction
    explicit CallTipsList(QPlainTextEdit* parent);
    /// Destruction
    ~CallTipsList() override;

    void keyboardSearch(const QString& wordPrefix) override;
    void showTips(const QString&);
    void validateCursor();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;

private Q_SLOTS:
    void callTipItemActivated(QListWidgetItem* item);

private:
    QString extractContext(const QString&) const;
    QMap<QString, CallTip> extractTips(const QString&) const;
    QMap<QString, CallTip> publicTips(const QMap<QString, CallTip>& tips) const;
    QMap<QString, CallTip> privateTips(const QMap<QString, CallTip>& tips) const;
    void addTips(const QMap<QString, CallTip>& tips);
    void extractTipsFromObject(const Py::Object&, const Py::List&, QMap<QString, CallTip>&) const;
    CallTip extractTipsFromAttribute(const Py::Object&, const QString&) const;
    void extractTipsFromProperties(Py::Object&, QMap<QString, CallTip>&) const;
    QString stripWhiteSpace(const QString&) const;
    Py::Object getAttrWorkaround(const Py::Object& obj, const Py::String& name) const;
    void tryGetDocString(const Py::Object& help, CallTip& tip) const;
    void tryGetTipFromDocString(const QString&, CallTip& tip) const;

private:
    QPlainTextEdit* textEdit;
    int cursorPos {0};
    mutable bool validObject {true};
    bool doCallCompletion {false};
    QList<int> hideKeys;
    QList<int> compKeys;
};

}  // namespace Gui

#endif  // GUI_CALLTIPS_H
