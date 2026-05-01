/*
 * Copyright 2013 Christian Loose <christian.loose@hamburg.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "optionsdialog.h"
#include "ui_optionsdialog.h"

#include <QFontComboBox>
#include <QFontDatabase>
#include <QHeaderView>
#include <QItemEditorFactory>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QTableWidgetItem>
#include <QAction>
//#include <QDebug>
#include <QFileDialog>

#include <snippets/snippetcollection.h>
#include "options.h"
#include "snippetstablemodel.h"

class KeySequenceTableItem : public QTableWidgetItem
{
public:
    KeySequenceTableItem(const QKeySequence &keySequence) :
        QTableWidgetItem(QTableWidgetItem::UserType + 1),
        m_keySequence(keySequence)
    {
    }

    QVariant data(int role) const
    {
        switch (role) {
            case Qt::DisplayRole:
                return m_keySequence.toString();
            case Qt::EditRole:
                return m_keySequence;
            default:
                return QVariant();
        }
    }

    void setData(int role, const QVariant &data)
    {
        if (role == Qt::EditRole)
            m_keySequence = data.value<QKeySequence>();
        QTableWidgetItem::setData(role, data);
    }

private:
    QKeySequence m_keySequence;
};

class KeySequenceEditFactory : public QItemEditorCreatorBase
{
public:
    QWidget *createWidget(QWidget *parent) const
    {
        return new QKeySequenceEdit(parent);
    }

    QByteArray valuePropertyName() const
    {
        return QByteArrayLiteral("keySequence");
    }
};


OptionsDialog::OptionsDialog(Options *opt, SnippetCollection *collection, const QVector<QAction*> &acts, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::OptionsDialog),
    options(opt),
    snippetCollection(collection),
    actions(acts)
{
    ui->setupUi(this);

    ui->tabWidget->setIconSize(QSize(24, 24));
    ui->tabWidget->setTabIcon(0, QIcon(QStringLiteral("fa-cog.fontawesome")));
    ui->tabWidget->setTabIcon(1, QIcon(QStringLiteral("fa-file-text-o.fontawesome")));
    ui->tabWidget->setTabIcon(2, QIcon(QStringLiteral("fa-html5.fontawesome")));
    ui->tabWidget->setTabIcon(3, QIcon(QStringLiteral("fa-eye.fontawesome")));
    ui->tabWidget->setTabIcon(4, QIcon(QStringLiteral("fa-puzzle-piece.fontawesome")));
    ui->tabWidget->setTabIcon(5, QIcon(QStringLiteral("fa-globe.fontawesome")));
    ui->tabWidget->setTabIcon(7, QIcon(QStringLiteral("fa-keyboard-o.fontawesome")));

    const auto sizes = QFontDatabase::standardSizes();
    for (int size : sizes) {
        ui->defaultSizeComboBox->addItem(QString().setNum(size));
        ui->defaultFixedSizeComboBox->addItem(QString().setNum(size));
    }

    ui->snippetListView->setModel(new SnippetsTableModel(snippetCollection, ui->snippetListView));
    connect(ui->snippetListView->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &OptionsDialog::currentSnippetChanged);
    connect(ui->snippetContentPlainTextEdit, &QPlainTextEdit::textChanged, this, &OptionsDialog::snippetTextChanged);
    connect(ui->snippetTriggerLineEdit, &QLineEdit::editingFinished, this, &OptionsDialog::snippetTriggerEditingFinished);

    setupShortcutsTable();

    connect(ui->manualProxyRadioButton, &QRadioButton::toggled, this, &OptionsDialog::manualProxyRadioButtonToggled);
    connect(ui->addSnippetButton, &QPushButton::clicked, this, &OptionsDialog::addSnippetButtonClicked);
    connect(ui->removeSnippetButton, &QPushButton::clicked, this, &OptionsDialog::removeSnippetButtonClicked);
    connect(ui->pathBrowseButton, &QPushButton::clicked, this, &OptionsDialog::onPathBrowserButtonClicked);

    // read configuration state
    readState();
}

OptionsDialog::~OptionsDialog()
{
    delete ui;
}

void OptionsDialog::done(int result)
{
    if (result == QDialog::Accepted) {
        // save configuration state
        saveState();
    }

    QDialog::done(result);
}

void OptionsDialog::manualProxyRadioButtonToggled(bool checked)
{
    ui->proxyHostNameLabel->setEnabled(checked);
    ui->proxyHostNameLineEdit->setEnabled(checked);
    ui->proxyPortLabel->setEnabled(checked);
    ui->proxyPortSpinBox->setEnabled(checked);
    ui->proxyUserNameLabel->setEnabled(checked);
    ui->proxyUserNameLineEdit->setEnabled(checked);
    ui->proxyPasswordLabel->setEnabled(checked);
    ui->proxyPasswordLineEdit->setEnabled(checked);
}

void OptionsDialog::currentSnippetChanged(const QModelIndex &current, const QModelIndex &)
{
    if (!current.isValid()) {
        ui->snippetGroupBox->setEnabled(false);
        ui->snippetTriggerLineEdit->clear();
        ui->snippetContentPlainTextEdit->clear();
        ui->removeSnippetButton->setEnabled(false);
        return;
    }

    const Snippet snippet = snippetCollection->at(current.row());
    const QSignalBlocker triggerBlocker(ui->snippetTriggerLineEdit);
    const QSignalBlocker contentBlocker(ui->snippetContentPlainTextEdit);

    // update text edit for snippet content
    QString formattedSnippet(snippet.snippet);
    formattedSnippet.insert(snippet.cursorPosition, QStringLiteral("$|"));
    ui->snippetGroupBox->setEnabled(true);
    ui->snippetTriggerLineEdit->setText(snippet.trigger);
    ui->snippetTriggerLineEdit->setReadOnly(snippet.builtIn);
    ui->snippetContentPlainTextEdit->setPlainText(formattedSnippet);
    ui->snippetContentPlainTextEdit->setReadOnly(snippet.builtIn);

    // disable remove button when built-in snippet is selected
    ui->removeSnippetButton->setEnabled(!snippet.builtIn);
}

void OptionsDialog::snippetTextChanged()
{
    const QModelIndex &modelIndex = ui->snippetListView->selectionModel()->currentIndex();
    if (modelIndex.isValid()) {
        Snippet snippet = snippetCollection->at(modelIndex.row());
        if (!snippet.builtIn) {
            snippet.snippet = ui->snippetContentPlainTextEdit->toPlainText();

            // find cursor marker
            int pos = snippet.snippet.indexOf(QStringLiteral("$|"));
            if (pos >= 0) {
                snippet.cursorPosition = pos;
                snippet.snippet.remove(pos, 2);
            }

            snippetCollection->update(snippet);
        }
    }
}

void OptionsDialog::snippetTriggerEditingFinished()
{
    const QModelIndex &modelIndex = ui->snippetListView->selectionModel()->currentIndex();
    if (!modelIndex.isValid())
        return;

    Snippet snippet = snippetCollection->at(modelIndex.row());
    const QString newTrigger = ui->snippetTriggerLineEdit->text();
    if (snippet.builtIn || snippet.trigger == newTrigger)
        return;

    SnippetsTableModel *snippetModel = qobject_cast<SnippetsTableModel*>(ui->snippetListView->model());
    if (!snippetModel->setData(modelIndex, newTrigger, Qt::EditRole)) {
        if (ui->snippetListView->selectionModel()->currentIndex().isValid()) {
            ui->snippetTriggerLineEdit->setText(snippet.trigger);
        }
    }
}

void OptionsDialog::addSnippetButtonClicked()
{
    SnippetsTableModel *snippetModel = qobject_cast<SnippetsTableModel*>(ui->snippetListView->model());

    const QModelIndex &index = snippetModel->createSnippet();

    const int row = index.row();
    QModelIndex topLeft = snippetModel->index(row, 0, QModelIndex());
    QItemSelection selection(topLeft, topLeft);
    ui->snippetListView->selectionModel()->select(selection, QItemSelectionModel::SelectCurrent);
    ui->snippetListView->setCurrentIndex(topLeft);
    ui->snippetListView->scrollTo(topLeft);

    ui->snippetListView->edit(topLeft);
    ui->snippetTriggerLineEdit->setFocus();
    ui->snippetTriggerLineEdit->selectAll();
}

void OptionsDialog::removeSnippetButtonClicked()
{
    const QModelIndex &modelIndex = ui->snippetListView->selectionModel()->currentIndex();
    if (!modelIndex.isValid()) {
        QMessageBox::critical(0, tr("Error", "Title of error message box"), tr("No snippet selected."));
        return;
    }

    SnippetsTableModel *snippetModel = qobject_cast<SnippetsTableModel*>(ui->snippetListView->model());
    snippetModel->removeSnippet(modelIndex);
}

void OptionsDialog::validateShortcut(int row, int column)
{
    // Check changes to shortcut column only
    if (column != 1)
        return;

    QString newShortcut = ui->shortcutsTable->item(row, column)->text();
    QKeySequence ks(newShortcut);
    if (ks.isEmpty() && !newShortcut.isEmpty()) {
        // If new shortcut was invalid, restore the original
        ui->shortcutsTable->setItem(row, column,
            new KeySequenceTableItem(actions.at(row)->shortcut()));
    } else {
        // Check for conflicts.
        if (!ks.isEmpty()) {
            for (int c = 0; c < actions.size(); ++c) {
                if (c != row && ks == QKeySequence(ui->shortcutsTable->item(c, 1)->text())) {
                    ui->shortcutsTable->setItem(row, column,
                        new KeySequenceTableItem(actions.at(row)->shortcut()));
                    QMessageBox::information(this, tr("Conflict"),
                                             tr("This shortcut is already used for \"%1\"")
                                             .arg(actions.at(c)->text().remove(QLatin1Char('&'))));
                    return;
                }
            }
        }
        // If the new shortcut is not the same as the default, make the
        // action label bold.
        QFont font = ui->shortcutsTable->item(row, 0)->font();
        font.setBold(ks != actions.at(row)->property("defaultshortcut").value<QKeySequence>());
        ui->shortcutsTable->item(row, 0)->setFont(font);
    }
}

void OptionsDialog::onPathBrowserButtonClicked()
{
    QFileDialog dialog(this, tr("Choose directory"));
    dialog.setFileMode(QFileDialog::Directory);
    if (dialog.exec() == DialogCode::Accepted) {
        ui->pathLineEdit->setText(dialog.directory().path());
    }
}

void OptionsDialog::setupShortcutsTable()
{
    QStyledItemDelegate *delegate = new QStyledItemDelegate(ui->shortcutsTable);
    QItemEditorFactory *factory = new QItemEditorFactory();
    factory->registerEditor(static_cast<QMetaType::Type>(QMetaType::fromName("QKeySequence").id()), new KeySequenceEditFactory());
    delegate->setItemEditorFactory(factory);
    ui->shortcutsTable->setItemDelegateForColumn(1, delegate);

    ui->shortcutsTable->setRowCount(actions.size());

    int i = 0;
    for (const QAction *action : std::as_const(actions)) {
        QTableWidgetItem *label = new QTableWidgetItem(action->text().remove('&'));
        label->setFlags(Qt::ItemIsSelectable);
        const QKeySequence &defaultKeySeq = action->property("defaultshortcut").value<QKeySequence>();
        if (action->shortcut() != defaultKeySeq) {
            QFont font = label->font();
            font.setBold(true);
            label->setFont(font);
        }
        QTableWidgetItem *accel = new KeySequenceTableItem(action->shortcut());
        QTableWidgetItem *def = new QTableWidgetItem(defaultKeySeq.toString());
        def->setFlags(Qt::ItemIsSelectable);
        ui->shortcutsTable->setItem(i, 0, label);
        ui->shortcutsTable->setItem(i, 1, accel);
        ui->shortcutsTable->setItem(i, 2, def);
        ++i;
    }

    ui->shortcutsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui->shortcutsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    ui->shortcutsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

    connect(ui->shortcutsTable, &QTableWidget::cellChanged, this, &OptionsDialog::validateShortcut);
}

void OptionsDialog::readState()
{
    // general settings
    ui->converterComboBox->setCurrentIndex(options->markdownConverter());
    ui->pathLineEdit->setText(options->explorerDefaultPath());
    ui->useCurrentFilePathCheckbox->setChecked(options->willUseCurrentFilePath());

    // editor settings
    QFont font = options->editorFont();
    ui->fontComboBox->setCurrentFont(font);
    ui->fontSizeSpinBox->setValue(font.pointSize());
    ui->sourceSingleSizedCheckBox->setChecked(options->isSourceAtSingleSizeEnabled());
    ui->tabWidthSpinBox->setValue(options->tabWidth());
    ui->lineColumnCheckBox->setChecked(options->isLineColumnEnabled());
    ui->rulerEnableCheckBox->setChecked(options->isRulerEnabled());
    ui->rulerPosSpinBox->setValue(options->rulerPos());

    // html preview settings
    ui->standardFontComboBox->setCurrentFont(options->standardFont());
    ui->defaultSizeComboBox->setCurrentText(QString().setNum(options->defaultFontSize()));
    ui->serifFontComboBox->setCurrentFont(options->serifFont());
    ui->sansSerifFontComboBox->setCurrentFont(options->sansSerifFont());
    ui->fixedFontComboBox->setCurrentFont(options->fixedFont());
    ui->defaultFixedSizeComboBox->setCurrentText(QString().setNum(options->defaultFixedFontSize()));
    ui->mathInlineCheckBox->setChecked(options->isMathInlineSupportEnabled());
    ui->mathSupportCheckBox->setChecked(options->isMathSupportEnabled());

    // proxy settings
    switch (options->proxyMode()) {
    case Options::NoProxy:
        ui->noProxyRadioButton->setChecked(true);
        break;
    case Options::SystemProxy:
        ui->systemProxyRadioButton->setChecked(true);
        break;
    case Options::ManualProxy:
        ui->manualProxyRadioButton->setChecked(true);
        break;
    }
    ui->proxyHostNameLineEdit->setText(options->proxyHost());
    ui->proxyPortSpinBox->setValue(options->proxyPort());
    ui->proxyUserNameLineEdit->setText(options->proxyUser());
    ui->proxyPasswordLineEdit->setText(options->proxyPassword());
    manualProxyRadioButtonToggled(ui->manualProxyRadioButton->isChecked());

    // shortcut settings
    for (int i = 0; i < ui->shortcutsTable->rowCount(); ++i) {
        if (options->hasCustomShortcut(actions.at(i)->objectName())) {
            ui->shortcutsTable->item(i, 1)->setData(Qt::EditRole, options->customShortcut(actions.at(i)->objectName()));
        }
    }
}

void OptionsDialog::saveState()
{
    // general settings
    options->setMarkdownConverter((Options::MarkdownConverter)ui->converterComboBox->currentIndex());
    options->setExplorerDefaultPath(ui->pathLineEdit->text());
    options->setWillUseCurrentFilePath(ui->useCurrentFilePathCheckbox->isChecked());

    // editor settings
    QFont font = ui->fontComboBox->currentFont();
    font.setPointSize(ui->fontSizeSpinBox->value());
    options->setEditorFont(font);
    options->setSourceAtSingleSizeEnabled(ui->sourceSingleSizedCheckBox->isChecked());
    options->setTabWidth(ui->tabWidthSpinBox->value());
    options->setLineColumnEnabled(ui->lineColumnCheckBox->isChecked());
    options->setRulerEnabled(ui->rulerEnableCheckBox->isChecked());
    options->setRulerPos(ui->rulerPosSpinBox->value());
    options->setMathInlineSupportEnabled(ui->mathInlineCheckBox->isChecked());
    options->setMathSupportEnabled(ui->mathSupportCheckBox->isChecked());

    // html preview settings
    options->setStandardFont(ui->standardFontComboBox->currentFont());
    options->setDefaultFontSize(ui->defaultSizeComboBox->currentText().toInt());
    options->setSerifFont(ui->serifFontComboBox->currentFont());
    options->setSansSerifFont(ui->sansSerifFontComboBox->currentFont());
    options->setFixedFont(ui->fixedFontComboBox->currentFont());
    options->setDefaultFixedFontSize(ui->defaultFixedSizeComboBox->currentText().toInt());

    // proxy settings
    if (ui->noProxyRadioButton->isChecked()) {
        options->setProxyMode(Options::NoProxy);
    } else if (ui->systemProxyRadioButton->isChecked()) {
        options->setProxyMode(Options::SystemProxy);
    } else if (ui->manualProxyRadioButton->isChecked()) {
        options->setProxyMode(Options::ManualProxy);
    }
    options->setProxyHost(ui->proxyHostNameLineEdit->text());
    options->setProxyPort(ui->proxyPortSpinBox->value());
    options->setProxyUser(ui->proxyUserNameLineEdit->text());
    options->setProxyPassword(ui->proxyPasswordLineEdit->text());

    // shortcut settings
    for (int i = 0; i < ui->shortcutsTable->rowCount(); ++i) {
        QKeySequence customKeySeq(ui->shortcutsTable->item(i, 1)->text());
        options->addCustomShortcut(actions.at(i)->objectName(), customKeySeq);
    }

    options->apply();
}
