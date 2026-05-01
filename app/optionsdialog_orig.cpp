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
#include <QItemSelectionModel>
#include <QMessageBox>
#include <QAction>
#include <QFileDialog>

#include <snippets/snippetcollection.h>
#include "options.h"
#include "snippetstablemodel.h"

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
    ui->tabWidget->setTabIcon(3, QIcon(QStringLiteral("fa-puzzle-piece.fontawesome")));
    ui->tabWidget->setTabIcon(4, QIcon(QStringLiteral("fa-globe.fontawesome")));
    ui->tabWidget->setTabIcon(5, QIcon(QStringLiteral("fa-keyboard-o.fontawesome")));

    ui->snippetListView->setModel(new SnippetsTableModel(snippetCollection, ui->snippetListView));
    connect(ui->snippetListView->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &OptionsDialog::currentSnippetChanged);
    connect(ui->snippetContentPlainTextEdit, &QPlainTextEdit::textChanged, this, &OptionsDialog::snippetTextChanged);

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
    ui->proxyHostNameLineEdit->setEnabled(checked);
    ui->proxyPortSpinBox->setEnabled(checked);
}

void OptionsDialog::currentSnippetChanged(const QModelIndex &current, const QModelIndex &)
{
    const Snippet snippet = snippetCollection->at(current.row());

    // update text edit for snippet content
    QString formattedSnippet(snippet.snippet);
    formattedSnippet.insert(snippet.cursorPosition, QStringLiteral("$|"));
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

void OptionsDialog::onPathBrowserButtonClicked()
{
    QFileDialog dialog(this, tr("Choose directory"));
    dialog.setFileMode(QFileDialog::Directory);
    if (dialog.exec() == DialogCode::Accepted) {
        ui->pathLineEdit->setText(dialog.directory().path());
    }
}

void OptionsDialog::readState()
{
    // general settings
    ui->converterComboBox->setCurrentIndex(options->markdownConverter());
    ui->pathLineEdit->setText(options->explorerDefaultPath());

    // editor settings
    QFont font = options->editorFont();
    ui->fontComboBox->setCurrentFont(font);
    ui->fontSizeSpinBox->setValue(font.pointSize());
    ui->tabWidthSpinBox->setValue(options->tabWidth());
    ui->rulerEnableCheckBox->setChecked(options->isRulerEnabled());
    ui->rulerPosSpinBox->setValue(options->rulerPos());

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
}

void OptionsDialog::saveState()
{
    // general settings
    options->setMarkdownConverter((Options::MarkdownConverter)ui->converterComboBox->currentIndex());
    options->setExplorerDefaultPath(ui->pathLineEdit->text());

    // editor settings
    QFont font = ui->fontComboBox->currentFont();
    font.setPointSize(ui->fontSizeSpinBox->value());
    options->setEditorFont(font);
    options->setTabWidth(ui->tabWidthSpinBox->value());
    options->setRulerEnabled(ui->rulerEnableCheckBox->isChecked());
    options->setRulerPos(ui->rulerPosSpinBox->value());

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

    options->apply();
}

