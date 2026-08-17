#pragma once

#include <QDialog>
#include <QString>

class QLineEdit;
class QPushButton;

namespace jyd {

class ModelSelectionDialog final : public QDialog {
public:
    explicit ModelSelectionDialog(QWidget* parent = nullptr);

    QString selectedFile() const;
    QString selectedTextureFile() const;

protected:
    void accept() override;

private:
    void browse();
    void browseTexture();
    void updateConfirmState();

    QLineEdit* pathEdit_ = nullptr;
    QLineEdit* texturePathEdit_ = nullptr;
    QPushButton* confirmButton_ = nullptr;
};

} // namespace jyd
