#include "model_selection_dialog.hpp"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace jyd {

ModelSelectionDialog::ModelSelectionDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Select models and textures"));
    setModal(true);
    setMinimumWidth(560);

    auto* description = new QLabel(
        tr("Add one or more OBJ models with their corresponding textures."),
        this);
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

    addButton_ = new QPushButton(tr("Add pair"), this);
    addButton_->setEnabled(false);
    removeButton_ = new QPushButton(tr("Remove selected"), this);
    removeButton_->setEnabled(false);

    auto* pairButtonLayout = new QHBoxLayout;
    pairButtonLayout->addWidget(addButton_);
    pairButtonLayout->addWidget(removeButton_);
    pairButtonLayout->addStretch(1);

    auto* addedLabel = new QLabel(tr("Models to render:"), this);
    selectionList_ = new QListWidget(this);
    selectionList_->setMinimumHeight(120);
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
    layout->addLayout(pairButtonLayout);
    layout->addWidget(addedLabel);
    layout->addWidget(selectionList_);
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
    connect(addButton_, &QPushButton::clicked,
            this, [this] { addSelection(); });
    connect(removeButton_, &QPushButton::clicked,
            this, [this] { removeSelected(); });
    connect(selectionList_, &QListWidget::currentRowChanged,
            this, [this](int row) {
                removeButton_->setEnabled(row >= 0);
            });
}

const std::vector<ModelTextureSelection>&
ModelSelectionDialog::selections() const {
    return selections_;
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

bool ModelSelectionDialog::currentSelectionValid() const {
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

    return modelValid && textureValid;
}

void ModelSelectionDialog::addSelection() {
    if (!currentSelectionValid()) {
        return;
    }

    const QString modelPath = QFileInfo(pathEdit_->text()).absoluteFilePath();
    const QString texturePath =
        QFileInfo(texturePathEdit_->text()).absoluteFilePath();

    selections_.push_back({modelPath, texturePath});
    selectionList_->addItem(
        QFileInfo(modelPath).fileName() +
        QStringLiteral("  ->  ") +
        QFileInfo(texturePath).fileName());

    pathEdit_->clear();
    texturePathEdit_->clear();
    updateConfirmState();
}

void ModelSelectionDialog::removeSelected() {
    const int row = selectionList_->currentRow();
    if (row < 0 || static_cast<std::size_t>(row) >= selections_.size()) {
        return;
    }

    selections_.erase(selections_.begin() + row);
    delete selectionList_->takeItem(row);
    updateConfirmState();
}

void ModelSelectionDialog::updateConfirmState() {
    const bool currentValid = currentSelectionValid();
    const bool currentEmpty =
        pathEdit_->text().isEmpty() && texturePathEdit_->text().isEmpty();

    addButton_->setEnabled(currentValid);
    confirmButton_->setEnabled(
        currentValid || (!selections_.empty() && currentEmpty));
}

void ModelSelectionDialog::accept() {
    if (currentSelectionValid()) {
        addSelection();
    }

    if (!pathEdit_->text().isEmpty() || !texturePathEdit_->text().isEmpty()) {
        QMessageBox::warning(
            this, tr("Incomplete resource pair"),
            tr("Select both an OBJ model and its texture, or add the completed pair."));
        return;
    }

    if (selections_.empty()) {
        QMessageBox::warning(
            this, tr("No models"),
            tr("Add at least one model and texture pair."));
        return;
    }

    QDialog::accept();
}

} // namespace jyd
