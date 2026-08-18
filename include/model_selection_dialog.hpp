#pragma once

#include <QDialog>
#include <QString>
#include <vector>

class QLineEdit;
class QListWidget;
class QPushButton;

namespace jyd {

struct ModelTextureSelection {
    QString modelFile;
    QString textureFile;
};

class ModelSelectionDialog final : public QDialog {
public:
    explicit ModelSelectionDialog(QWidget* parent = nullptr);

    const std::vector<ModelTextureSelection>& selections() const;

protected:
    void accept() override;

private:
    void browse();
    void browseTexture();
    void addSelection();
    void removeSelected();
    bool currentSelectionValid() const;
    void updateConfirmState();

    QLineEdit* pathEdit_ = nullptr;
    QLineEdit* texturePathEdit_ = nullptr;
    QListWidget* selectionList_ = nullptr;
    QPushButton* addButton_ = nullptr;
    QPushButton* removeButton_ = nullptr;
    QPushButton* confirmButton_ = nullptr;
    std::vector<ModelTextureSelection> selections_;
};

} // namespace jyd
