/***************************************************************************
 *   Copyright (c) 2024 Ondsel <development@ondsel.com>                    *
 *                                                                         *
 *   This file is part of FreeCAD.                                         *
 *                                                                         *
 *   FreeCAD is free software: you can redistribute it and/or modify it    *
 *   under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1 of the  *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   FreeCAD is distributed in the hope that it will be useful, but        *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU      *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with FreeCAD. If not, see                               *
 *   <https://www.gnu.org/licenses/>.                                      *
 *                                                                         *
 **************************************************************************/

#include "PreCompiled.h"
#ifndef _PreComp_
#include <QMessageBox>
#include <QString>
#endif

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/ExpressionParser.h>
#include <App/PropertyUnits.h>
#include <Base/Tools.h>

#include "Dialogs/DlgAddPropertyVarSet.h"
#include "ui_DlgAddPropertyVarSet.h"
#include "MainWindow.h"
#include "ViewProviderVarSet.h"
#include "propertyeditor/PropertyItem.h"

FC_LOG_LEVEL_INIT("DlgAddPropertyVarSet", true, true)  // NOLINT

using namespace Gui;
using namespace Gui::Dialog;

const std::string DlgAddPropertyVarSet::GROUP_BASE = "Base";

namespace Transaction
{
    const bool Commit = false;
    const bool Abort = true;
}

namespace Editor
{
    const bool KeepName = false;
    const bool ClearName = true;
}

/* TRANSLATOR Gui::Dialog::DlgAddPropertyVarSet */

DlgAddPropertyVarSet::DlgAddPropertyVarSet(QWidget* parent,
                                           ViewProviderVarSet* viewProvider)
    : QDialog(parent),
      varSet(viewProvider->getObject<App::VarSet>()),
      ui(new Ui_DlgAddPropertyVarSet),
      comboBoxGroup(this),
      completerType(this),
      editor(nullptr),
      transactionID(0)
{
    ui->setupUi(this);

    initializeWidgets(viewProvider);
}

DlgAddPropertyVarSet::~DlgAddPropertyVarSet() = default;

void DlgAddPropertyVarSet::initializeGroup()
{
    comboBoxGroup.setObjectName(QStringLiteral("comboBoxGroup"));
    comboBoxGroup.setInsertPolicy(QComboBox::InsertAtTop);
    comboBoxGroup.setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    auto formLayout = qobject_cast<QFormLayout*>(layout());
    formLayout->setWidget(1, QFormLayout::FieldRole, &comboBoxGroup);

    std::vector<App::Property*> properties;
    varSet->getPropertyList(properties);
    std::unordered_set<std::string> groupNames;
    for (auto prop : properties) {
        const char* groupName = varSet->getPropertyGroup(prop);
        groupNames.insert(groupName ? groupName : GROUP_BASE);
    }
    std::vector<std::string> groupNamesSorted(groupNames.begin(), groupNames.end());
    std::sort(groupNamesSorted.begin(), groupNamesSorted.end(), [](const std::string& a,
                                                                   const std::string& b)
    {
        // prefer anything else other than Base, so move it to the back
        if (a == GROUP_BASE) {
            return false;
        }
        if (b == GROUP_BASE) {
            return true;
        }
        return a < b;
    });

    for (const auto& groupName : groupNamesSorted) {
        comboBoxGroup.addItem(QString::fromStdString(groupName));
    }

    comboBoxGroup.setEditText(QString::fromStdString(groupNamesSorted[0]));
    connComboBoxGroup = connect(&comboBoxGroup, &EditFinishedComboBox::editFinished,
                                this, &DlgAddPropertyVarSet::onEditFinished);
}

void DlgAddPropertyVarSet::getSupportedTypes(std::vector<Base::Type>& types)
{
    std::vector<Base::Type> proptypes;
    Base::Type::getAllDerivedFrom(Base::Type::fromName("App::Property"), proptypes);
    std::copy_if(proptypes.begin(), proptypes.end(), std::back_inserter(types), [](const Base::Type& type) {
        return type.canInstantiate();
    });
    std::sort(types.begin(), types.end(), [](Base::Type a, Base::Type b) {
        return strcmp(a.getName(), b.getName()) < 0;
    });
}

void DlgAddPropertyVarSet::initializeTypes()
{
    auto paramGroup = App::GetApplication().GetParameterGroupByPath(
            "User parameter:BaseApp/Preferences/PropertyView");
    auto lastType = Base::Type::fromName(
            paramGroup->GetASCII("NewPropertyType","App::PropertyLength").c_str());
    if(lastType.isBad()) {
        lastType = App::PropertyLength::getClassTypeId();
    }

    std::vector<Base::Type> types;
    getSupportedTypes(types);

    for(const auto& type : types) {
        ui->comboBoxType->addItem(QString::fromLatin1(type.getName()));
        if (type == lastType) {
            ui->comboBoxType->setCurrentIndex(ui->comboBoxType->count()-1);
        }
    }

    completerType.setModel(ui->comboBoxType->model());
    completerType.setCaseSensitivity(Qt::CaseInsensitive);
    completerType.setFilterMode(Qt::MatchContains);
    ui->comboBoxType->setCompleter(&completerType);
    ui->comboBoxType->setInsertPolicy(QComboBox::NoInsert);

    connComboBoxType = connect(ui->comboBoxType, &QComboBox::currentTextChanged,
                               this, &DlgAddPropertyVarSet::onEditFinished);
}

void DlgAddPropertyVarSet::initializeWidgets(ViewProviderVarSet* viewProvider)
{
    initializeGroup();
    initializeTypes();

    connect(this, &QDialog::finished,
            this, [viewProvider](int result) { viewProvider->onFinished(result); });
    connLineEditNameEditFinished = connect(ui->lineEditName, &QLineEdit::editingFinished,
                                           this, &DlgAddPropertyVarSet::onEditFinished);
    connLineEditNameTextChanged = connect(ui->lineEditName, &QLineEdit::textChanged,
            this, &DlgAddPropertyVarSet::onNamePropertyChanged);

    setTitle();
    setOkEnabled(false);

    ui->lineEditName->setFocus();

    QWidget::setTabOrder(ui->lineEditName, &comboBoxGroup);
    QWidget::setTabOrder(&comboBoxGroup, ui->comboBoxType);
}

void DlgAddPropertyVarSet::setTitle()
{
    setWindowTitle(tr("Add a property to %1").arg(QString::fromStdString(varSet->getFullName())));
}

void DlgAddPropertyVarSet::setOkEnabled(bool enabled)
{
    QPushButton *okButton = ui->buttonBox->button(QDialogButtonBox::Ok);
    okButton->setEnabled(enabled);
}

void DlgAddPropertyVarSet::clearEditors(bool clearName)
{
    if (clearName) {
        QSignalBlocker block(ui->lineEditName);
        ui->lineEditName->clear();
    }
    removeEditor();
    ui->lineEditToolTip->clear();
    setOkEnabled(false);
    namePropertyToAdd.clear();
}

void DlgAddPropertyVarSet::removeEditor()
{
    if (editor) {
        layout()->removeWidget(editor.get());
        QWidget::setTabOrder(ui->comboBoxType, ui->checkBoxAdd);
        editor = nullptr;
    }
}

void DlgAddPropertyVarSet::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
        setTitle();
    }
    QDialog::changeEvent(e);
}

static PropertyEditor::PropertyItem *createPropertyItem(App::Property *prop)
{
    const char *editor = prop->getEditorName();
    if (Base::Tools::isNullOrEmpty(editor)) {
        return nullptr;
    }
    auto item = static_cast<PropertyEditor::PropertyItem*>(
            PropertyEditor::PropertyItemFactory::instance().createPropertyItem(editor));
    if (!item) {
        qWarning("No property item for type %s found\n", editor);
    }
    return item;
}

void DlgAddPropertyVarSet::addEditor(PropertyEditor::PropertyItem* propertyItem,
                                     [[maybe_unused]]std::string& type)
{
    editor.reset(propertyItem->createEditor(this, [this]() {
        this->valueChanged();
    }));
    propertyItem->setEditorData(editor.get(), propertyItem->data(1, Qt::EditRole));
    editor->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    editor->setObjectName(QStringLiteral("editor"));
    auto formLayout = qobject_cast<QFormLayout*>(layout());
    formLayout->setWidget(3, QFormLayout::FieldRole, editor.get());

    QWidget::setTabOrder(ui->comboBoxType, editor.get());
    QWidget::setTabOrder(editor.get(), ui->checkBoxAdd);
}

bool DlgAddPropertyVarSet::isTypeWithEditor(const std::string& type)
{
    return typesWithoutEditor.find(type) == typesWithoutEditor.end();
}

void DlgAddPropertyVarSet::createProperty()
{
    std::string name = ui->lineEditName->text().toStdString();
    std::string group = comboBoxGroup.currentText().toStdString();
    std::string type = ui->comboBoxType->currentText().toStdString();
    std::string doc = ui->lineEditToolTip->text().toStdString();

    App::Property* prop = nullptr;
    try {
        prop = varSet->addDynamicProperty(type.c_str(), name.c_str(),
                                          group.c_str(), doc.c_str());
    }
    catch (const Base::Exception& e) {
        e.ReportException();
        critical(tr("Add property"),
                 tr("Failed to add property to '%1': %2").arg(
                         QString::fromLatin1(varSet->getFullName().c_str()),
                         QString::fromUtf8(e.what())));
        clearEditors(Editor::ClearName);
        return;
    }

    namePropertyToAdd = name;

    objectIdentifier = std::make_unique<App::ObjectIdentifier>(*prop);
    // creating a propertyItem here because it has all kinds of logic for
    // editors that we can reuse
    removeEditor();
    propertyItem.reset(createPropertyItem(prop));
    if (propertyItem && isTypeWithEditor(type)) {
        propertyItem->setPropertyData({prop});
        propertyItem->bind(*objectIdentifier);
        addEditor(propertyItem.get(), type);
    }

    setOkEnabled(true);
}

App::Property* DlgAddPropertyVarSet::getPropertyToAdd()
{
    // This function should be called only if it is certain the property exists.
    // It will throw a runtime error if not.
    App::Property* prop = varSet->getPropertyByName(namePropertyToAdd.c_str());
    if (prop == nullptr) {
        FC_THROWM(Base::RuntimeError, "A property with name '" << namePropertyToAdd << "' does not exist.");
    }

    return prop;
}

void DlgAddPropertyVarSet::changePropertyToAdd() {
    // we were already adding a new property, the only option to get here
    // is a change of type or group.

    std::string name = ui->lineEditName->text().toStdString();
    assert(name == namePropertyToAdd);

    // performs a check for nullptr
    App::Property* prop = getPropertyToAdd();

    std::string group = comboBoxGroup.currentText().toStdString();
    std::string doc = ui->lineEditToolTip->text().toStdString();
    if (prop->getGroup() != group) {
        varSet->changeDynamicProperty(prop, group.c_str(), doc.c_str());
    }

    std::string type = ui->comboBoxType->currentText().toStdString();
    if (prop->getTypeId() != Base::Type::fromName(type.c_str())) {
        // the property should have a different type
        varSet->removeDynamicProperty(namePropertyToAdd.c_str());
        createProperty();
    }
}


/* We use these functions rather than the functions provided by App::Document
 * because this dialog may be opened when another transaction is in progress.
 * An example is opening a sketch.  If this dialog uses the functions provided
 * by App::Document, a reject of the dialog would close that transaction.  By
 * checking whether the transaction ID is "our" transaction ID, we prevent this
 * behavior.
 */
void DlgAddPropertyVarSet::openTransaction()
{
    transactionID = App::GetApplication().setActiveTransaction("Add property VarSet");
}


bool DlgAddPropertyVarSet::hasPendingTransaction()
{
    return transactionID != 0;
}


void DlgAddPropertyVarSet::closeTransaction(bool abort)
{
    if (transactionID != 0) {
        App::GetApplication().closeActiveTransaction(abort, transactionID);
        transactionID = 0;
    }
}

void DlgAddPropertyVarSet::clearCurrentProperty()
{
    removeEditor();
    varSet->removeDynamicProperty(namePropertyToAdd.c_str());
    if (hasPendingTransaction()) {
        closeTransaction(Transaction::Abort);
    }
    setOkEnabled(false);
    namePropertyToAdd.clear();
}

namespace
{
class CreatePropertyException : public std::exception {
public:
    explicit CreatePropertyException(std::string message)
        : msg(std::move(message))
    {}

    const char* what() const noexcept override
    {
        return msg.c_str();
    }

private:
    std::string msg;
};
}

void DlgAddPropertyVarSet::checkName() {
    std::string name = ui->lineEditName->text().toStdString();
    if(name.empty() || name != Base::Tools::getIdentifier(name)) {
        QMessageBox::critical(getMainWindow(),
                              tr("Invalid name"),
                              tr("The property name must only contain alpha numericals, "
                                 "underscore, and must not start with a digit."));
        clearEditors(Editor::KeepName);
        throw CreatePropertyException("Invalid name");
    }

    if(App::ExpressionParser::isTokenAUnit(name) || App::ExpressionParser::isTokenAConstant(name)) {
        critical(tr("Invalid name"),
                 tr("The property name is a reserved word."));
        clearEditors(Editor::KeepName);
        throw CreatePropertyException("Invalid name");
    }

    if (namePropertyToAdd.empty()) {
        // we are adding a new property, check whether it doesn't already exist
        auto prop = varSet->getPropertyByName(name.c_str());
        if(prop && prop->getContainer() == varSet) {
            critical(tr("Invalid name"),
                     tr("The property '%1' already exists in '%2'").arg(
                             QString::fromLatin1(name.c_str()),
                             QString::fromLatin1(varSet->getFullName().c_str())));
            clearEditors(Editor::KeepName);
            throw CreatePropertyException("Invalid name");
        }
    }
}

void DlgAddPropertyVarSet::checkGroup() {
    std::string group = comboBoxGroup.currentText().toStdString();

    if (group.empty() || group != Base::Tools::getIdentifier(group)) {
        critical(tr("Invalid name"),
                 tr("The group name must only contain alpha numericals,\n"
                    "underscore, and must not start with a digit."));
        comboBoxGroup.setEditText(QStringLiteral("Base"));
        throw CreatePropertyException("Invalid name");
    }
}

void DlgAddPropertyVarSet::checkType() {
    std::string type = ui->comboBoxType->currentText().toStdString();

    if (Base::Type::fromName(type.c_str()).isBad()) {
        throw CreatePropertyException("Invalid name");
    }
}

void DlgAddPropertyVarSet::onEditFinished() {
    /* The editor for the value is dynamically created if 1) the name has been
     * determined and 2) if the type of the property has been determined.  The
     * group of the property is important too, but it is not essential, because
     * we can change the group after the property has been created.
     *
     * In this function we check whether we can create a property and therefore
     * an editor.
     */

    try {
        checkName();
        checkGroup();
        checkType();
        // no check for tooltip, we accept any string
    }
    catch (const CreatePropertyException&) {
        if (!namePropertyToAdd.empty()) {
            clearCurrentProperty();
        }
        return;
    }

    if (namePropertyToAdd.empty()) {
        // we are adding a new property
        openTransaction();
        createProperty();
    }
    else {
        // we were already adding a new property that should now be changed
        changePropertyToAdd();
    }
}

void DlgAddPropertyVarSet::onNamePropertyChanged(const QString& text)
{
    if (!namePropertyToAdd.empty() && text.toStdString() != namePropertyToAdd) {
        // The user decided to change the name of the property.  This
        // invalidates the editor that is strictly associated with the property.
        clearCurrentProperty();
    }
}

void DlgAddPropertyVarSet::critical(const QString& title, const QString& text) {
    static bool criticalDialogShown = false;
    if (!criticalDialogShown) {
        criticalDialogShown = true;
        QMessageBox::critical(this, title, text);
        criticalDialogShown = false;
    }
}

void DlgAddPropertyVarSet::valueChanged()
{
    QVariant data;
    data = propertyItem->editorData(editor.get());
    propertyItem->setData(data);
}

void DlgAddPropertyVarSet::addDocumentation() {
    /* Add the documentation to an existing property.
     * Note that this method assumes the property exists.
     *
     * Since there is no check on documentation (we accept any string), there
     * is no signal handler for the documentation field.  This method updates
     * the property that is being added with the text inserted as
     * documentation/tooltip.
     *
     * This function should be called at a late stage, before doing the accept.
     */

    std::string group = comboBoxGroup.currentText().toStdString();
    std::string doc = ui->lineEditToolTip->text().toStdString();

    // performs a check for nullptr
    App::Property* prop = getPropertyToAdd();
    varSet->changeDynamicProperty(prop, group.c_str(), doc.c_str());
}

void DlgAddPropertyVarSet::accept()
{
    addDocumentation();
    closeTransaction(Transaction::Commit);

    if (ui->checkBoxAdd->isChecked()) {
        clearEditors(Editor::ClearName);
        openTransaction();
        ui->lineEditName->setFocus();
        return;
    }

    std::string group = comboBoxGroup.currentText().toStdString();
    std::string type = ui->comboBoxType->currentText().toStdString();
    auto paramGroup = App::GetApplication().GetParameterGroupByPath(
            "User parameter:BaseApp/Preferences/PropertyView");
    paramGroup->SetASCII("NewPropertyType", type.c_str());
    paramGroup->SetASCII("NewPropertyGroup", group.c_str());
    QDialog::accept();
}

void DlgAddPropertyVarSet::reject()
{
    // On reject we can disconnect the signal handlers because nothing useful
    // is to be done.  Otherwise, signals may activate the handlers that assume
    // that a new property has been created, an assumption that will be
    // violated by aborting the transaction because it will remove the newly
    // created property.
    disconnect(connComboBoxGroup);
    disconnect(connComboBoxType);
    disconnect(connLineEditNameEditFinished);
    disconnect(connLineEditNameTextChanged);

    // a transaction is not pending if a name has not been determined.
    if (hasPendingTransaction()) {
        closeTransaction(Transaction::Abort);
    }
    QDialog::reject();
}


#include "moc_DlgAddPropertyVarSet.cpp"
