#include "model_selection_dialog.hpp"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace jyd {

ModelSelectionDialog::ModelSelectionDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Select model"));
    setModal(true);
    setMinimumWidth(560);

    auto* description = new QLabel(
        tr("Choose an OBJ model before starting the renderer."), this);
    pathEdit_ = new QLineEdit(this);
    pathEdit_->setReadOnly(true);
    pathEdit_->setPlaceholderText(tr("No model selected"));

    auto* browseButton = new QPushButton(tr("Browse..."), this);
    auto* pathLayout = new QHBoxLayout;
    pathLayout->addWidget(pathEdit_, 1);
    pathLayout->addWidget(browseButton);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    confirmButton_ = buttons->button(QDialogButtonBox::Ok);
    confirmButton_->setText(tr("Render"));
    confirmButton_->setEnabled(false);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(description);
    layout->addLayout(pathLayout);
    layout->addWidget(buttons);

    connect(browseButton, &QPushButton::clicked,
            this, [this] { browse(); });
    connect(buttons, &QDialogButtonBox::accepted,
            this, &ModelSelectionDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected,
            this, &ModelSelectionDialog::reject);
}

QString ModelSelectionDialog::selectedFile() const {
    return pathEdit_->text();
}

void ModelSelectionDialog::browse() {
    const QString filename = QFileDialog::getOpenFileName(
        this,
        tr("Select OBJ model"),
        QString(),
        tr("Wavefront OBJ (*.obj);;All files (*)"));
    if (!filename.isEmpty()) {
        pathEdit_->setText(QFileInfo(filename).absoluteFilePath());
    }
    updateConfirmState();
}

void ModelSelectionDialog::updateConfirmState() {
    const QFileInfo file(pathEdit_->text());
    confirmButton_->setEnabled(
        file.exists() && file.isFile() && file.isReadable() &&
        file.suffix().compare(QStringLiteral("obj"), Qt::CaseInsensitive) == 0);
}

void ModelSelectionDialog::accept() {
    const QFileInfo file(pathEdit_->text());
    if (!file.exists() || !file.isFile() || !file.isReadable()) {
        QMessageBox::warning(
            this, tr("Invalid model"),
            tr("The selected file does not exist or cannot be read."));
        return;
    }
    if (file.suffix().compare(QStringLiteral("obj"), Qt::CaseInsensitive) != 0) {
        QMessageBox::warning(
            this, tr("Unsupported model"),
            tr("Please select a Wavefront OBJ file."));
        return;
    }
    QDialog::accept();
}

} // namespace jyd
