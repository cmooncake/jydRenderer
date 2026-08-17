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

    auto* textureLabel = new QLabel(tr("Texture image:"), this);

    texturePathEdit_ = new QLineEdit(this);
    texturePathEdit_->setReadOnly(true);
    texturePathEdit_->setPlaceholderText(tr("No texture selected"));

    auto* textureBrowseButton =
        new QPushButton(tr("Browse texture..."), this);

    auto* texturePathLayout = new QHBoxLayout;
    texturePathLayout->addWidget(texturePathEdit_, 1);
    texturePathLayout->addWidget(textureBrowseButton);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    confirmButton_ = buttons->button(QDialogButtonBox::Ok);
    confirmButton_->setText(tr("Render"));
    confirmButton_->setEnabled(false);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(description);
    layout->addLayout(pathLayout);
    layout->addWidget(textureLabel);
    layout->addLayout(texturePathLayout);
    layout->addWidget(buttons);

    connect(browseButton, &QPushButton::clicked,
            this, [this] { browse(); });
    connect(buttons, &QDialogButtonBox::accepted,
            this, &ModelSelectionDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected,
            this, &ModelSelectionDialog::reject);
    connect(
        textureBrowseButton,
        &QPushButton::clicked,
        this,
        [this] { browseTexture(); });
}

QString ModelSelectionDialog::selectedFile() const {
    return pathEdit_->text();
}

QString ModelSelectionDialog::selectedTextureFile() const {
    return texturePathEdit_->text();
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

void ModelSelectionDialog::browseTexture() {
    const QString filename = QFileDialog::getOpenFileName(
        this,
        tr("Select texture image"),
        QString(),
        tr("Images (*.png *.jpg *.jpeg *.bmp *.tga);;All files (*)"));

    if (!filename.isEmpty()) {
        texturePathEdit_->setText(
            QFileInfo(filename).absoluteFilePath());
    }

    updateConfirmState();
}

void ModelSelectionDialog::updateConfirmState() {
    const QFileInfo modelFile(pathEdit_->text());
    const QFileInfo textureFile(texturePathEdit_->text());

    const bool modelValid =
        modelFile.exists() &&
        modelFile.isFile() &&
        modelFile.isReadable() &&
        modelFile.suffix().compare(
            QStringLiteral("obj"),
            Qt::CaseInsensitive) == 0;

    const bool textureValid =
        textureFile.exists() &&
        textureFile.isFile() &&
        textureFile.isReadable();

    confirmButton_->setEnabled(modelValid && textureValid);
}

void ModelSelectionDialog::accept() {
    const QFileInfo modelFile(pathEdit_->text());
    if (!modelFile.exists() || !modelFile.isFile() || !modelFile.isReadable()) {
        QMessageBox::warning(
            this, tr("Invalid model"),
            tr("The selected file does not exist or cannot be read."));
        return;
    }
    if (modelFile.suffix().compare(
            QStringLiteral("obj"), Qt::CaseInsensitive) != 0) {
        QMessageBox::warning(
            this, tr("Unsupported model"),
            tr("Please select a Wavefront OBJ file."));
        return;
    }

    const QFileInfo textureFile(texturePathEdit_->text());
    if (!textureFile.exists() ||
        !textureFile.isFile() ||
        !textureFile.isReadable()) {
        QMessageBox::warning(
            this, tr("Invalid texture"),
            tr("The selected texture does not exist or cannot be read."));
        return;
    }

    QDialog::accept();
}

} // namespace jyd
